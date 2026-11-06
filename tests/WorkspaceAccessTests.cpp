#include "tools/WorkspaceAccess.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <cstring>
#include <memory>
#include <thread>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

using titan::WorkspaceAccess;

namespace {

bool put(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

QByteArray get(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

QString errorCode(const QJsonObject &result)
{
    return result.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
}

QString sha(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool directoryLink(const QString &link, const QString &target)
{
#ifdef Q_OS_WIN
    // A directory junction generally works without Developer Mode or elevation.
    const QString nativeLink = QDir::toNativeSeparators(link);
    const QString nativeTarget = QDir::toNativeSeparators(QFileInfo(target).absoluteFilePath());
    if (!CreateDirectoryW(reinterpret_cast<LPCWSTR>(nativeLink.utf16()), nullptr)) return false;
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(nativeLink.utf16()), GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) { RemoveDirectoryW(reinterpret_cast<LPCWSTR>(nativeLink.utf16())); return false; }
    const QString substitute = QStringLiteral("\\??\\") + nativeTarget;
    const WORD substituteBytes = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    const WORD printBytes = static_cast<WORD>(nativeTarget.size() * sizeof(wchar_t));
    struct JunctionBuffer {
        DWORD tag;
        WORD dataLength;
        WORD reserved;
        WORD substituteOffset;
        WORD substituteLength;
        WORD printOffset;
        WORD printLength;
        wchar_t names[1];
    };
    const DWORD total = 16 + substituteBytes + sizeof(wchar_t) + printBytes + sizeof(wchar_t);
    QByteArray storage(total, '\0');
    auto *buffer = reinterpret_cast<JunctionBuffer *>(storage.data());
    buffer->tag = IO_REPARSE_TAG_MOUNT_POINT;
    buffer->dataLength = static_cast<WORD>(total - 8);
    buffer->substituteLength = substituteBytes;
    buffer->printOffset = substituteBytes + sizeof(wchar_t);
    buffer->printLength = printBytes;
    memcpy(buffer->names, substitute.utf16(), substituteBytes);
    memcpy(reinterpret_cast<char *>(buffer->names) + buffer->printOffset, nativeTarget.utf16(), printBytes);
    DWORD returned = 0;
    const bool success = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer, total, nullptr, 0, &returned, nullptr) != FALSE;
    CloseHandle(handle);
    if (!success) RemoveDirectoryW(reinterpret_cast<LPCWSTR>(nativeLink.utf16()));
    return success;
#else
    return QFile::link(target, link);
#endif
}

bool fileLink(const QString &link, const QString &target)
{
#ifdef Q_OS_WIN
    const QString nativeLink = QDir::toNativeSeparators(link);
    const QString nativeTarget = QDir::toNativeSeparators(target);
    if (CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(nativeLink.utf16()), reinterpret_cast<LPCWSTR>(nativeTarget.utf16()), 0x2))
        return true;
    return CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(nativeLink.utf16()), reinterpret_cast<LPCWSTR>(nativeTarget.utf16()), 0) != FALSE;
#else
    return QFile::link(target, link);
#endif
}

} // namespace

class WorkspaceAccessTests final : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        m_temporary = std::make_unique<QTemporaryDir>();
        QVERIFY(m_temporary->isValid());
        m_root = m_temporary->path() + QStringLiteral("/workspace");
        m_data = m_temporary->path() + QStringLiteral("/private-data");
        m_outside = m_temporary->path() + QStringLiteral("/outside");
        m_sibling = m_temporary->path() + QStringLiteral("/workspace-evil");
        for (const QString &directory : {m_root, m_data, m_outside, m_sibling}) QVERIFY(QDir().mkpath(directory));
        QVERIFY(put(m_root + QStringLiteral("/sample.txt"), "hello world\nsecond line\n"));
        QVERIFY(put(m_outside + QStringLiteral("/secret.txt"), "outside secret"));
        QVERIFY(put(m_data + QStringLiteral("/secret.txt"), "private secret"));
        QVERIFY(put(m_sibling + QStringLiteral("/secret.txt"), "prefix sibling secret"));
        m_access = std::make_unique<WorkspaceAccess>(QStringList{m_data});
    }

    void cleanup()
    {
        m_access.reset();
        // Remove junction entries themselves before QTemporaryDir walks the
        // owned tree. Never recursively remove a junction's target.
        for (const QString &link : std::as_const(m_directoryLinks)) {
#ifdef Q_OS_WIN
            const QString native = QDir::toNativeSeparators(link);
            QVERIFY(RemoveDirectoryW(reinterpret_cast<LPCWSTR>(native.utf16())) != FALSE);
#else
            QVERIFY(QFile::remove(link));
#endif
        }
        m_directoryLinks.clear();
        m_temporary.reset();
    }

    void deniesNoGrantOutsideSiblingAndTraversal()
    {
        const QString file = m_root + QStringLiteral("/sample.txt");
        QCOMPARE(errorCode(read(file)), QStringLiteral("access_denied"));
        QCOMPARE(errorCode(call("files_list", {{"path", m_root}})), QStringLiteral("access_denied"));
        QCOMPARE(errorCode(call("files_search", {{"path", m_root}, {"query", "hello"}})), QStringLiteral("access_denied"));
        grant();
        QCOMPARE(errorCode(read(m_outside + QStringLiteral("/secret.txt"))), QStringLiteral("access_denied"));
        QCOMPARE(errorCode(read(m_sibling + QStringLiteral("/secret.txt"))), QStringLiteral("access_denied"));
        QCOMPARE(errorCode(call("files_write", {{"path", m_outside + "/new.txt"}, {"content", "denied"}})), QStringLiteral("access_denied"));
        QCOMPARE(errorCode(read(m_root + QStringLiteral("/../outside/secret.txt"))), QStringLiteral("invalid_path"));
        QCOMPARE(errorCode(read(QStringLiteral("sample.txt"))), QStringLiteral("invalid_path"));
        QCOMPARE(m_access->grants().size(), 1);
    }

    void readOnlyGrant()
    {
        grant(false);
        const QString file = m_root + QStringLiteral("/sample.txt");
        const auto result = read(file);
        QVERIFY(result.value("ok").toBool());
        QCOMPARE(result.value("content").toString(), QStringLiteral("hello world\nsecond line\n"));
        QCOMPARE(errorCode(call("files_write", {{"path", m_root + "/new.txt"}, {"content", "new"}})), QStringLiteral("read_only"));
        QCOMPARE(errorCode(call("files_write", {{"path", file}, {"content", "changed"}, {"expected_sha256", result.value("sha256")}})), QStringLiteral("read_only"));
        QCOMPARE(errorCode(call("files_replace", {{"path", file}, {"old_text", "hello"}, {"new_text", "bye"}, {"expected_sha256", result.value("sha256")}})), QStringLiteral("read_only"));
        QCOMPARE(get(file), QByteArray("hello world\nsecond line\n"));
    }

    void createsAndUpdatesWithPrivateBackup()
    {
        grant();
        const QString file = m_root + QStringLiteral("/new.txt");
        const auto created = call("files_write", {{"path", file}, {"content", QString::fromUtf8("Salut \xC3\xA9\n")}});
        QVERIFY2(created.value("ok").toBool(), qPrintable(errorCode(created)));
        QVERIFY(created.value("created").toBool());
        QVERIFY(!created.value("backup_created").toBool());
        const QByteArray original("Salut \xC3\xA9\n");
        QCOMPARE(get(file), original);
        QCOMPARE(created.value("sha256").toString(), sha(original));
        QCOMPARE(errorCode(call("files_write", {{"path", file}, {"content", "overwrite"}})), QStringLiteral("hash_required"));
        // Knowing a hash without reading through this tool is insufficient.
        QCOMPARE(errorCode(call("files_write", {{"path", file}, {"content", "overwrite"}, {"expected_sha256", sha(original)}})), QStringLiteral("stale_hash"));
        const auto prior = read(file);
        const auto updated = call("files_write", {{"path", file}, {"content", "updated\n"}, {"expected_sha256", prior.value("sha256")}});
        QVERIFY(updated.value("ok").toBool());
        QVERIFY(updated.value("backup_created").toBool());
        QCOMPARE(get(file), QByteArray("updated\n"));
        const QDir backups(m_data + QStringLiteral("/workspace-backups"));
        const auto files = backups.entryList(QDir::Files);
        QCOMPARE(files.size(), 1);
        QCOMPARE(get(backups.filePath(files.first())), original);
        QCOMPARE(errorCode(read(backups.filePath(files.first()))), QStringLiteral("access_denied"));
        QCOMPARE(errorCode(call("files_write", {{"path", file}, {"content", "again"}, {"expected_sha256", updated.value("sha256")}})), QStringLiteral("stale_hash"));
    }

    void rejectsStaleOrInvalidatedHashes()
    {
        grant();
        const QString file = m_root + QStringLiteral("/sample.txt");
        auto prior = read(file);
        QVERIFY(put(file, "external modification"));
        QCOMPARE(errorCode(call("files_write", {{"path", file}, {"content", "lost update"}, {"expected_sha256", prior.value("sha256")}})), QStringLiteral("stale_hash"));
        QCOMPARE(get(file), QByteArray("external modification"));
        prior = read(file);
        grant(); // Permission changes invalidate previous read authorization.
        QCOMPARE(errorCode(call("files_write", {{"path", file}, {"content", "lost update"}, {"expected_sha256", prior.value("sha256")}})), QStringLiteral("stale_hash"));
        prior = read(file);
        QVERIFY(call("files_write", {{"path", file}, {"content", "fresh update"}, {"expected_sha256", prior.value("sha256")}}).value("ok").toBool());
    }

    void exactUniqueReplacement()
    {
        grant();
        const QString file = m_root + QStringLiteral("/sample.txt");
        QVERIFY(put(file, "aaa unique\n"));
        const auto prior = read(file);
        const auto replace = [&](const QString &before, const QString &after) {
            return call("files_replace", {{"path", file}, {"old_text", before}, {"new_text", after}, {"expected_sha256", prior.value("sha256")}});
        };
        QCOMPARE(errorCode(replace("missing", "x")), QStringLiteral("replacement_not_found"));
        QCOMPARE(errorCode(replace("aa", "x")), QStringLiteral("replacement_not_unique"));
        QCOMPARE(errorCode(replace("", "x")), QStringLiteral("invalid_arguments"));
        QVERIFY(replace("unique", "changed").value("ok").toBool());
        QCOMPARE(get(file), QByteArray("aaa changed\n"));
    }

    void protectsApplicationDataUnderAncestorGrant()
    {
        m_access->setGrants({{m_temporary->path(), true}});
        QVERIFY(read(m_root + QStringLiteral("/sample.txt")).value("ok").toBool());
        QCOMPARE(errorCode(read(m_data + QStringLiteral("/secret.txt"))), QStringLiteral("access_denied"));
        QCOMPARE(errorCode(call("files_write", {{"path", m_data + "/new.txt"}, {"content", "denied"}})), QStringLiteral("access_denied"));
        const auto listed = call("files_list", {{"path", m_temporary->path()}, {"recursive", true}});
        QVERIFY(listed.value("ok").toBool());
        for (const auto &entry : listed.value("entries").toArray())
            QVERIFY(!entry.toObject().value("path").toString().contains("private-data"));
        const auto searched = call("files_search", {{"path", m_temporary->path()}, {"query", "private secret"}});
        QVERIFY(searched.value("matches").toArray().isEmpty());
        m_access->setGrants({{m_data, true}});
        QVERIFY(m_access->grants().isEmpty());
    }

    void refusesBinaryAndOversizeText()
    {
        grant();
        const QString binary = m_root + QStringLiteral("/binary.bin");
        const QString invalid = m_root + QStringLiteral("/invalid.txt");
        const QString large = m_root + QStringLiteral("/large.txt");
        const QString maximum = m_root + QStringLiteral("/maximum.txt");
        QVERIFY(put(binary, QByteArray("a\0b", 3)));
        QVERIFY(put(invalid, QByteArray::fromHex("c328")));
        QVERIFY(put(large, QByteArray(WorkspaceAccess::MaxTextBytes + 1, 'x')));
        QVERIFY(put(maximum, QByteArray(WorkspaceAccess::MaxTextBytes, 'z')));
        QCOMPARE(errorCode(read(binary)), QStringLiteral("binary_file"));
        QCOMPARE(errorCode(read(invalid)), QStringLiteral("binary_file"));
        QCOMPARE(errorCode(read(large)), QStringLiteral("too_large"));
        QVERIFY(read(maximum).value("ok").toBool());
        QCOMPARE(errorCode(call("files_write", {{"path", m_root + "/new.txt"}, {"content", QString(WorkspaceAccess::MaxTextBytes + 1, u'x')}})), QStringLiteral("too_large"));
        QCOMPARE(errorCode(call("files_write", {{"path", m_root + "/new.txt"}, {"content", QString(QChar::Null)}})), QStringLiteral("binary_file"));
        const auto found = call("files_search", {{"path", m_root}, {"query", "hello"}});
        QVERIFY(found.value("ok").toBool());
        QCOMPARE(found.value("matches").toArray().size(), 1);
        QVERIFY(found.value("files_skipped").toInt() >= 3);
    }

    void preservesUtf8BomDuringReplacement()
    {
        grant();
        const QString file = m_root + QStringLiteral("/bom.txt");
        QVERIFY(put(file, QByteArray::fromHex("efbbbf") + "hello"));
        const auto prior = read(file);
        QVERIFY(prior.value("ok").toBool());
        QVERIFY(call("files_replace", {{"path", file}, {"old_text", "hello"}, {"new_text", "world"}, {"expected_sha256", prior.value("sha256")}}).value("ok").toBool());
        QCOMPARE(get(file), QByteArray::fromHex("efbbbf") + "world");
    }

    void boundedListingAndSearch()
    {
        grant();
        QVERIFY(QDir().mkpath(m_root + QStringLiteral("/nested")));
        QVERIFY(put(m_root + QStringLiteral("/nested/extra.txt"), "HELLO\nhello\nhello\n"));
        const auto list = call("files_list", {{"path", m_root}, {"recursive", true}, {"max_entries", 1}});
        QCOMPARE(list.value("entries").toArray().size(), 1);
        QVERIFY(list.value("truncated").toBool());
        const auto search = call("files_search", {{"path", m_root}, {"query", "hello"}, {"max_results", 1}});
        QCOMPARE(search.value("matches").toArray().size(), 1);
        QVERIFY(search.value("truncated").toBool());
        const auto sensitive = call("files_search", {{"path", m_root + "/nested"}, {"query", "HELLO"}, {"case_sensitive", true}});
        QCOMPARE(sensitive.value("matches").toArray().size(), 1);
        QCOMPARE(sensitive.value("matches").toArray().first().toObject().value("line").toInt(), 1);
        QVERIFY(put(m_root + QStringLiteral("/long.txt"), QByteArray(10000, 'a') + "needle" + QByteArray(10000, 'a')));
        const auto excerpt = call("files_search", {{"path", m_root}, {"query", "needle"}}).value("matches").toArray();
        QCOMPARE(excerpt.size(), 1);
        QVERIFY(excerpt.first().toObject().value("text").toString().size() <= 500);
    }

    void depthBoundAndSymlinkCycles()
    {
        grant();
        QString nested = m_root;
        for (int i = 0; i < WorkspaceAccess::MaxDepth + 3; ++i) nested += QStringLiteral("/d");
        QVERIFY(QDir().mkpath(nested));
        QVERIFY(put(nested + QStringLiteral("/hidden.txt"), "depth needle"));
        const auto result = call("files_search", {{"path", m_root}, {"query", "depth needle"}});
        QVERIFY(result.value("truncated").toBool());
        QVERIFY(result.value("matches").toArray().isEmpty());
    }

    void searchFileAndByteBudgets()
    {
        grant();
        const QString many = m_root + QStringLiteral("/many");
        QVERIFY(QDir().mkpath(many));
        for (int i = 0; i < WorkspaceAccess::MaxSearchFiles + 1; ++i)
            QVERIFY(put(many + u'/' + QString::number(i) + QStringLiteral(".txt"), ""));
        const auto files = call("files_search", {{"path", many}, {"query", "absent"}});
        QVERIFY(files.value("truncated").toBool());
        QCOMPARE(files.value("files_scanned").toInt(), WorkspaceAccess::MaxSearchFiles);
        const QString large = m_root + QStringLiteral("/large-files");
        QVERIFY(QDir().mkpath(large));
        for (int i = 0; i < 17; ++i)
            QVERIFY(put(large + u'/' + QString::number(i) + QStringLiteral(".txt"), QByteArray(WorkspaceAccess::MaxTextBytes, 'x')));
        const auto bytes = call("files_search", {{"path", large}, {"query", "absent"}});
        QVERIFY(bytes.value("truncated").toBool());
        QCOMPARE(bytes.value("files_scanned").toInt(), 16);
    }

    void junctionEscapesAndProtectedAliases()
    {
        grant();
        const QString escape = m_root + QStringLiteral("/escape");
        if (!directoryLink(escape, m_outside)) QSKIP("Directory junction/symlink creation is unavailable on this filesystem.");
        m_directoryLinks.append(escape);
        const QString protectedAlias = m_root + QStringLiteral("/protected-alias");
        QVERIFY(directoryLink(protectedAlias, m_data));
        m_directoryLinks.append(protectedAlias);
        const QString cycle = m_root + QStringLiteral("/cycle");
        QVERIFY(directoryLink(cycle, m_root));
        m_directoryLinks.append(cycle);
        QCOMPARE(errorCode(read(escape + QStringLiteral("/secret.txt"))), QStringLiteral("access_denied"));
        QCOMPARE(errorCode(call("files_write", {{"path", escape + "/new.txt"}, {"content", "denied"}})), QStringLiteral("access_denied"));
        QCOMPARE(errorCode(read(m_root + QStringLiteral("/protected-alias/secret.txt"))), QStringLiteral("access_denied"));
        const auto found = call("files_search", {{"path", m_root}, {"query", "secret"}});
        QVERIFY(found.value("matches").toArray().isEmpty());
        QVERIFY(!found.value("truncated").toBool());
        const auto listed = call("files_list", {{"path", m_root}, {"recursive", true}});
        QVERIFY(!listed.value("truncated").toBool());
        for (const auto &entry : listed.value("entries").toArray()) {
            const QString path = entry.toObject().value("path").toString();
            QVERIFY(!path.startsWith(m_outside));
            QVERIFY(!path.startsWith(m_data));
        }
    }

    void fileSymlinkEscape()
    {
        grant();
        const QString link = m_root + QStringLiteral("/file-link.txt");
        if (!fileLink(link, m_outside + QStringLiteral("/secret.txt"))) QSKIP("File symlink creation requires Windows Developer Mode or privilege.");
        QCOMPARE(errorCode(read(link)), QStringLiteral("access_denied"));
        QCOMPARE(errorCode(call("files_write", {{"path", link}, {"content", "denied"}})), QStringLiteral("access_denied"));
        QCOMPARE(get(m_outside + QStringLiteral("/secret.txt")), QByteArray("outside secret"));
    }

    void failedBackupDoesNotChangeOriginal()
    {
        grant();
        const QString file = m_root + QStringLiteral("/sample.txt");
        const auto prior = read(file);
        QVERIFY(put(m_data + QStringLiteral("/workspace-backups"), "block directory creation"));
        QCOMPARE(errorCode(call("files_write", {{"path", file}, {"content", "changed"}, {"expected_sha256", prior.value("sha256")}})), QStringLiteral("backup_failed"));
        QCOMPARE(get(file), QByteArray("hello world\nsecond line\n"));
        QVERIFY(QDir(m_root).entryList({QStringLiteral(".titan-save-*")}, QDir::Files | QDir::Hidden).isEmpty());
    }

    void rejectsInvalidArgumentsAndWindowsAliases()
    {
        grant();
        QCOMPARE(errorCode(call("shell", {})), QStringLiteral("unknown_tool"));
        QCOMPARE(errorCode(call("files_read", {{"path", 5}})), QStringLiteral("invalid_arguments"));
        QCOMPARE(errorCode(call("files_read", {{"path", m_root}, {"extra", true}})), QStringLiteral("invalid_arguments"));
        QCOMPARE(errorCode(call("files_list", {{"path", m_root}, {"max_entries", 1.5}})), QStringLiteral("invalid_arguments"));
        QCOMPARE(errorCode(call("files_search", {{"path", m_root}, {"query", ""}})), QStringLiteral("invalid_arguments"));
        QCOMPARE(errorCode(call("files_search", {{"path", m_root}, {"query", "a\nb"}})), QStringLiteral("invalid_arguments"));
#ifdef Q_OS_WIN
        QCOMPARE(errorCode(read(m_root + QStringLiteral("/sample.txt:stream"))), QStringLiteral("invalid_path"));
        QCOMPARE(errorCode(read(m_root + QStringLiteral("/sample.txt."))), QStringLiteral("invalid_path"));
        QCOMPARE(errorCode(read(QStringLiteral("\\\\?\\") + QDir::toNativeSeparators(m_root) + QStringLiteral("\\sample.txt"))), QStringLiteral("invalid_path"));
        QString differentCase = QDir::toNativeSeparators(m_root + QStringLiteral("/sample.txt")).toUpper();
        QVERIFY(read(differentCase).value("ok").toBool());
#endif
    }

    void concurrentGrantChangesAreSerialized()
    {
        grant();
        std::atomic_bool invalid{false};
        const QString file = m_root + QStringLiteral("/sample.txt");
        std::thread reader([&] {
            for (int i = 0; i < 200; ++i) {
                const auto result = read(file);
                if (!result.value("ok").toBool() && errorCode(result) != QStringLiteral("access_denied")) invalid = true;
                const auto current = m_access->grants();
                if (current.size() > 1) invalid = true;
            }
        });
        for (int i = 0; i < 200; ++i) {
            m_access->setGrants({});
            grant();
        }
        reader.join();
        QVERIFY(!invalid.load());
        m_access->setGrants({});
        QCOMPARE(errorCode(read(file)), QStringLiteral("access_denied"));
    }

    void toolDefinitionsHavePreciseFunctionSchemas()
    {
        const auto definitions = WorkspaceAccess::toolDefinitions();
        QCOMPARE(definitions.size(), 5);
        QSet<QString> names;
        for (const auto &definition : definitions) {
            QCOMPARE(definition.toObject().value("type").toString(), QStringLiteral("function"));
            const auto function = definition.toObject().value("function").toObject();
            names.insert(function.value("name").toString());
            const auto parameters = function.value("parameters").toObject();
            QVERIFY(!parameters.value("additionalProperties").toBool(true));
            QVERIFY(parameters.value("properties").toObject().contains("path"));
        }
        QCOMPARE(names.size(), 5);
        QVERIFY(!names.contains(QStringLiteral("shell")));
    }

private:
    void grant(bool writable = true) { m_access->setGrants({{m_root, writable}}); }
    QJsonObject call(const char *tool, const QJsonObject &args) { return m_access->invoke(QString::fromLatin1(tool), args); }
    QJsonObject read(const QString &path) { return call("files_read", {{"path", path}}); }

    std::unique_ptr<QTemporaryDir> m_temporary;
    std::unique_ptr<WorkspaceAccess> m_access;
    QString m_root, m_data, m_outside, m_sibling;
    QStringList m_directoryLinks;
};

QTEST_GUILESS_MAIN(WorkspaceAccessTests)
#include "WorkspaceAccessTests.moc"
