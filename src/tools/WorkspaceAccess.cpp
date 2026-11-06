#include "WorkspaceAccess.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonValue>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTemporaryFile>
#include <QUuid>

#include <algorithm>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#endif

namespace titan {
namespace {

#ifdef Q_OS_WIN
constexpr auto PathCase = Qt::CaseInsensitive;
#else
constexpr auto PathCase = Qt::CaseSensitive;
#endif

QString normalized(QString path)
{
    path.replace(u'\\', u'/');
    path = QDir::cleanPath(path);
    while (path.endsWith(u'/') && path.size() > 1 && !QDir(path).isRoot())
        path.chop(1);
    return path;
}

QString pathKey(const QString &path)
{
#ifdef Q_OS_WIN
    return normalized(path).toCaseFolded();
#else
    return normalized(path);
#endif
}

#ifdef Q_OS_WIN
QString finalHandlePath(HANDLE handle)
{
    const DWORD size = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (size == 0 || size > 32768) return {};
    QString result(static_cast<qsizetype>(size), QChar::Null);
    const DWORD written = GetFinalPathNameByHandleW(handle, reinterpret_cast<LPWSTR>(result.data()), size,
                                                   FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (written == 0 || written >= size) return {};
    result.resize(written);
    if (result.startsWith(QStringLiteral("\\\\?\\UNC\\"), Qt::CaseInsensitive))
        result = QStringLiteral("//") + result.mid(8);
    else if (result.startsWith(QStringLiteral("\\\\?\\")))
        result.remove(0, 4);
    return normalized(result);
}
#endif

QString canonicalPath(const QString &path)
{
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(path);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    const QString result = finalHandlePath(handle);
    CloseHandle(handle);
    return result;
#else
    return QFileInfo(path).canonicalFilePath();
#endif
}

bool within(const QString &path, const QString &root)
{
    const QString prefix = root.endsWith(u'/') ? root : root + u'/';
    return path.compare(root, PathCase) == 0 || path.startsWith(prefix, PathCase);
}

QString resolvedRoot(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = canonicalPath(path);
    return normalized(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool safeAbsolutePath(QString input)
{
    input.replace(u'\\', u'/');
    if (input.isEmpty() || !QDir::isAbsolutePath(input) || input.contains(QChar::Null))
        return false;
    const auto parts = input.split(u'/', Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part == u"..")
            return false;
        for (const QChar c : part) {
            if (c.unicode() < 32)
                return false;
        }
    }
#ifdef Q_OS_WIN
    if (input.startsWith(u"//?/") || input.startsWith(u"//./"))
        return false;
    const bool driveAbsolute = input.size() >= 3 && input[0].isLetter() && input[1] == u':' && input[2] == u'/';
    const bool uncAbsolute = input.startsWith(u"//") && !input.startsWith(u"///") && parts.size() >= 2;
    if (!driveAbsolute && !uncAbsolute) return false;
    for (qsizetype i = 0; i < input.size(); ++i) {
        if (QStringView(u"<>\"|?*").contains(input[i]))
            return false;
        if (input[i] == u':' && !(i == 1 && input[0].isLetter()))
            return false; // Includes NTFS alternate data streams.
    }
    for (const QString &part : parts) {
        if (part == u"." || part.endsWith(u'.') || part.endsWith(u' '))
            return false;
        const QString base = part.section(u'.', 0, 0).toUpper();
        if (base == u"CON" || base == u"PRN" || base == u"AUX" || base == u"NUL"
            || (base.size() == 4 && (base.startsWith(u"COM") || base.startsWith(u"LPT"))
                && base[3] >= u'1' && base[3] <= u'9'))
            return false;
    }
#endif
    return true;
}

QString hash(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QJsonObject failure(const QString &code)
{
    // Deliberately omit file contents, OS messages, and protected destination paths.
    QString message = QStringLiteral("The filesystem operation could not be completed.");
    if (code == u"access_denied") message = QStringLiteral("The path is outside the granted folders or is protected.");
    else if (code == u"read_only") message = QStringLiteral("This folder has read-only permission.");
    else if (code == u"invalid_arguments") message = QStringLiteral("The tool arguments are invalid.");
    else if (code == u"invalid_path") message = QStringLiteral("Use an absolute path without traversal or device syntax.");
    else if (code == u"not_found") message = QStringLiteral("The path does not exist.");
    else if (code == u"not_directory") message = QStringLiteral("The path must be a directory.");
    else if (code == u"not_file") message = QStringLiteral("The path must be a regular file.");
    else if (code == u"binary_file") message = QStringLiteral("Only valid UTF-8 text files are supported.");
    else if (code == u"too_large") message = QStringLiteral("The text exceeds the 1 MiB limit.");
    else if (code == u"hash_required") message = QStringLiteral("Read the existing file and supply its exact expected_sha256.");
    else if (code == u"stale_hash") message = QStringLiteral("The file changed or the hash is not from its latest read. Read it again.");
    else if (code == u"already_exists") message = QStringLiteral("A new-file write cannot overwrite an existing file.");
    else if (code == u"replacement_not_found") message = QStringLiteral("old_text does not occur in the file.");
    else if (code == u"replacement_not_unique") message = QStringLiteral("old_text must occur exactly once in the file.");
    else if (code == u"backup_failed") message = QStringLiteral("The protected backup could not be saved. The file was not changed.");
    else if (code == u"path_changed") message = QStringLiteral("The filesystem path changed during the operation. Retry after checking the folder.");
    else if (code == u"unknown_tool") message = QStringLiteral("This filesystem tool is not available.");
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}};
}

bool argumentsValid(const QJsonObject &args, const QStringList &allowed,
                    const QStringList &required)
{
    for (auto it = args.begin(); it != args.end(); ++it) {
        if (!allowed.contains(it.key())) return false;
    }
    for (const QString &key : required) {
        if (!args.value(key).isString()) return false;
    }
    return true;
}

bool integerOption(const QJsonObject &args, const QString &key, int fallback, int maximum, int &value)
{
    if (!args.contains(key)) { value = fallback; return true; }
    const auto json = args.value(key);
    if (!json.isDouble() || json.toDouble() < 1 || json.toDouble() > maximum)
        return false;
    value = json.toInt(-1);
    return value > 0 && json.toDouble() == value;
}

bool textBytes(const QByteArray &bytes, QString &text, QString &code)
{
    if (bytes.size() > WorkspaceAccess::MaxTextBytes) { code = QStringLiteral("too_large"); return false; }
    for (const char c : bytes) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte == 0 || (byte < 32 && byte != 9 && byte != 10 && byte != 13)) {
            code = QStringLiteral("binary_file"); return false;
        }
    }
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::ConvertInitialBom);
    text = decoder(bytes);
    if (decoder.hasError()) { code = QStringLiteral("binary_file"); return false; }
    return true;
}

bool openedPathMatches(QFile &file, const QString &expected)
{
#ifdef Q_OS_WIN
    const intptr_t nativeHandle = _get_osfhandle(file.handle());
    if (nativeHandle == -1) return false;
    const QString finalPath = finalHandlePath(reinterpret_cast<HANDLE>(nativeHandle));
    return !finalPath.isEmpty() && finalPath.compare(expected, PathCase) == 0;
#else
    Q_UNUSED(file);
    Q_UNUSED(expected);
    return true;
#endif
}

bool readText(const QString &path, QByteArray &bytes, QString &text, QString &code,
              qint64 maximum = WorkspaceAccess::MaxTextBytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { code = QStringLiteral("io_error"); return false; }
    if (!openedPathMatches(file, path)) { code = QStringLiteral("path_changed"); return false; }
    if (file.size() > maximum) { code = QStringLiteral("too_large"); return false; }
    bytes = file.read(maximum + 1);
    if (file.error() != QFileDevice::NoError) { code = QStringLiteral("io_error"); return false; }
    return textBytes(bytes, text, code);
}

// On Windows, pin every existing canonical directory in the path without
// FILE_SHARE_DELETE. A junction/ancestor cannot be exchanged after authorization.
// Calls use the canonical path and recheck it after acquiring these handles.
class DirectoryPins final {
public:
    ~DirectoryPins()
    {
#ifdef Q_OS_WIN
        for (HANDLE handle : std::as_const(m_handles)) CloseHandle(handle);
#endif
    }
    bool acquire(const QString &directory)
    {
#ifdef Q_OS_WIN
        QStringList ancestors;
        QDir current(directory);
        for (;;) {
            ancestors.prepend(normalized(current.absolutePath()));
            if (current.isRoot()) break;
            if (!current.cdUp()) return false;
        }
        for (const QString &ancestor : std::as_const(ancestors)) {
            const QString native = QDir::toNativeSeparators(ancestor);
            HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_READ_ATTRIBUTES,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (handle == INVALID_HANDLE_VALUE) return false;
            m_handles.append(handle);
            const QString canonical = canonicalPath(ancestor);
            if (canonical.isEmpty() || normalized(canonical).compare(ancestor, PathCase) != 0)
                return false;
        }
#else
        Q_UNUSED(directory);
#endif
        return true;
    }
private:
#ifdef Q_OS_WIN
    QList<HANDLE> m_handles;
#endif
};

QJsonObject stringProperty(const QString &description)
{
    return {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), description}};
}

} // namespace

WorkspaceAccess::WorkspaceAccess(QStringList protectedRoots)
{
    if (protectedRoots.isEmpty())
        protectedRoots.append(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    for (const QString &root : std::as_const(protectedRoots)) {
        if (safeAbsolutePath(root)) {
            const QString lexical = normalized(QFileInfo(root).absoluteFilePath());
            if (!m_protectedRoots.contains(lexical)) m_protectedRoots.append(lexical);
            const QString canonical = resolvedRoot(root);
            if (!m_protectedRoots.contains(canonical)) m_protectedRoots.append(canonical);
        }
    }
    if (m_protectedRoots.isEmpty()) {
        // A bad configuration must fail closed, never produce unprotected backups.
        m_protectedRoots.append(normalized(QDir::rootPath()));
    }
    m_backupRoot = normalized(m_protectedRoots.first() + QStringLiteral("/workspace-backups"));
}

void WorkspaceAccess::setGrants(QList<FolderGrant> grants)
{
    QMutexLocker locker(&m_mutex);
    m_grants.clear();
    m_readHashes.clear();
    for (const FolderGrant &grant : std::as_const(grants)) {
        if (!safeAbsolutePath(grant.path)) continue;
        const QFileInfo info(grant.path);
        if (!info.exists() || !info.isDir()) continue;
        const QString canonical = canonicalPath(grant.path);
        if (canonical.isEmpty() || protectedPath(canonical)) continue;
        auto previous = std::find_if(m_grants.begin(), m_grants.end(), [&](const FolderGrant &item) {
            return item.path.compare(canonical, PathCase) == 0;
        });
        if (previous == m_grants.end()) m_grants.append({canonical, grant.writable});
        else previous->writable = previous->writable || grant.writable;
    }
}

QList<FolderGrant> WorkspaceAccess::grants() const
{
    QMutexLocker locker(&m_mutex);
    return m_grants;
}

bool WorkspaceAccess::protectedPath(const QString &path) const
{
    for (const QString &root : m_protectedRoots) {
        if (within(path, root) || within(path, resolvedRoot(root))) return true;
    }
    return within(path, m_backupRoot);
}

bool WorkspaceAccess::authorize(const QString &input, bool write, bool allowMissing,
                                AuthorizedPath &result, QString &code) const
{
    if (!safeAbsolutePath(input)) { code = QStringLiteral("invalid_path"); return false; }
    const QString lexical = normalized(input);
    if (protectedPath(lexical)) { code = QStringLiteral("access_denied"); return false; }
    QFileInfo info(lexical);
    QString canonical = canonicalPath(lexical);
    if (canonical.isEmpty()) {
        // A dangling symlink must never become a new-file write.
        if (info.isSymLink()) { code = QStringLiteral("access_denied"); return false; }
        const QFileInfo parent(info.absolutePath());
        const QString parentCanonical = canonicalPath(info.absolutePath());
        if (parentCanonical.isEmpty() || !parent.isDir()) { code = QStringLiteral("not_found"); return false; }
        canonical = normalized(parentCanonical + u'/' + info.fileName());
        result.exists = false;
    } else {
        canonical = normalized(canonical);
        result.exists = true;
    }
    if (protectedPath(canonical)) { code = QStringLiteral("access_denied"); return false; }
    bool readable = false;
    bool writable = false;
    for (const FolderGrant &grant : m_grants) {
        // A moved/replaced grant must be reselected in the UI.
        const QString current = canonicalPath(grant.path);
        if (current.isEmpty() || normalized(current).compare(grant.path, PathCase) != 0) continue;
        if (within(canonical, grant.path)) {
            readable = true;
            writable = writable || grant.writable;
        }
    }
    if (!readable) { code = QStringLiteral("access_denied"); return false; }
    if (write && !writable) { code = QStringLiteral("read_only"); return false; }
    if (!result.exists && !allowMissing) { code = QStringLiteral("not_found"); return false; }
    result.path = canonical;
    result.directory = result.exists && QFileInfo(canonical).isDir();
    if (result.exists && !result.directory && !QFileInfo(canonical).isFile()) {
        code = QStringLiteral("not_file"); return false;
    }
    return true;
}

QJsonObject WorkspaceAccess::invoke(const QString &tool, const QJsonObject &arguments)
{
    QMutexLocker locker(&m_mutex);
    if (tool == u"files_list") return list(arguments);
    if (tool == u"files_read") return read(arguments);
    if (tool == u"files_search") return search(arguments);
    if (tool == u"files_write") return write(arguments, false);
    if (tool == u"files_replace") return write(arguments, true);
    return failure(QStringLiteral("unknown_tool"));
}

QJsonObject WorkspaceAccess::list(const QJsonObject &args)
{
    if (!argumentsValid(args, {QStringLiteral("path"), QStringLiteral("recursive"), QStringLiteral("max_entries")}, {QStringLiteral("path")})
        || (args.contains(u"recursive") && !args.value(u"recursive").isBool()))
        return failure(QStringLiteral("invalid_arguments"));
    int maximum;
    if (!integerOption(args, QStringLiteral("max_entries"), 200, 1000, maximum)) return failure(QStringLiteral("invalid_arguments"));
    QString code;
    AuthorizedPath root;
    if (!authorize(args.value(u"path").toString(), false, false, root, code)) return failure(code);
    if (!root.directory) return failure(QStringLiteral("not_directory"));
    DirectoryPins pins;
    if (!pins.acquire(root.path)) return failure(QStringLiteral("path_changed"));
    QJsonArray entries;
    QList<QPair<QString, int>> pending{{root.path, 0}};
    QSet<QString> seen{pathKey(root.path)};
    int visited = 0;
    bool truncated = false;
    while (!pending.isEmpty() && !truncated) {
        const auto [directory, depth] = pending.takeFirst();
        DirectoryPins directoryPins;
        AuthorizedPath checkedDirectory;
        if (!directoryPins.acquire(directory) || !authorize(directory, false, false, checkedDirectory, code)
            || checkedDirectory.path.compare(directory, PathCase) != 0) continue;
        QDirIterator children(directory, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        while (children.hasNext()) {
            if (++visited > MaxTraversalEntries) { truncated = true; break; }
            const QFileInfo child(children.next());
            AuthorizedPath item;
            if (!authorize(child.absoluteFilePath(), false, false, item, code)) continue;
            if (entries.size() >= maximum) { truncated = true; break; }
            QJsonObject entry{{QStringLiteral("path"), item.path}, {QStringLiteral("name"), child.fileName()},
                              {QStringLiteral("type"), item.directory ? QStringLiteral("directory") : QStringLiteral("file")}};
            if (!item.directory) entry.insert(QStringLiteral("bytes"), static_cast<double>(QFileInfo(item.path).size()));
            entries.append(entry);
            if (args.value(u"recursive").toBool() && item.directory && !seen.contains(pathKey(item.path))) {
                seen.insert(pathKey(item.path));
                if (depth >= MaxDepth) truncated = true;
                else pending.append({item.path, depth + 1});
            }
        }
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), root.path},
            {QStringLiteral("entries"), entries}, {QStringLiteral("truncated"), truncated}};
}

QJsonObject WorkspaceAccess::read(const QJsonObject &args)
{
    if (!argumentsValid(args, {QStringLiteral("path")}, {QStringLiteral("path")})) return failure(QStringLiteral("invalid_arguments"));
    AuthorizedPath path;
    QString code;
    if (!authorize(args.value(u"path").toString(), false, false, path, code)) return failure(code);
    if (path.directory) return failure(QStringLiteral("not_file"));
    DirectoryPins pins;
    if (!pins.acquire(QFileInfo(path.path).absolutePath())) return failure(QStringLiteral("path_changed"));
    AuthorizedPath rechecked;
    if (!authorize(path.path, false, false, rechecked, code) || rechecked.path.compare(path.path, PathCase) != 0)
        return failure(QStringLiteral("path_changed"));
    QByteArray bytes;
    QString content;
    if (!readText(path.path, bytes, content, code)) return failure(code);
    const QString digest = hash(bytes);
    m_readHashes.insert(pathKey(path.path), digest);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), path.path}, {QStringLiteral("content"), content},
            {QStringLiteral("sha256"), digest}, {QStringLiteral("bytes"), static_cast<double>(bytes.size())}};
}

QJsonObject WorkspaceAccess::search(const QJsonObject &args)
{
    if (!argumentsValid(args, {QStringLiteral("path"), QStringLiteral("query"), QStringLiteral("case_sensitive"), QStringLiteral("max_results")},
                        {QStringLiteral("path"), QStringLiteral("query")})
        || args.value(u"query").toString().isEmpty() || args.value(u"query").toString().size() > 4096
        || args.value(u"query").toString().contains(u'\n') || args.value(u"query").toString().contains(u'\r')
        || (args.contains(u"case_sensitive") && !args.value(u"case_sensitive").isBool()))
        return failure(QStringLiteral("invalid_arguments"));
    int maximum;
    if (!integerOption(args, QStringLiteral("max_results"), 100, 500, maximum)) return failure(QStringLiteral("invalid_arguments"));
    QString code;
    AuthorizedPath root;
    if (!authorize(args.value(u"path").toString(), false, false, root, code)) return failure(code);
    const QString query = args.value(u"query").toString();
    const auto sensitivity = args.value(u"case_sensitive").toBool() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    QList<QPair<QString, int>> pending{{root.path, 0}};
    QSet<QString> seen;
    QJsonArray matches;
    int visited = 0, scanned = 0, skipped = 0;
    qint64 totalBytes = 0;
    bool truncated = false;
    while (!pending.isEmpty() && !truncated) {
        const auto [candidate, depth] = pending.takeFirst();
        if (++visited > MaxTraversalEntries) { truncated = true; break; }
        AuthorizedPath path;
        if (!authorize(candidate, false, false, path, code)) { ++skipped; continue; }
        if (seen.contains(pathKey(path.path))) continue;
        seen.insert(pathKey(path.path));
        DirectoryPins pins;
        if (!pins.acquire(path.directory ? path.path : QFileInfo(path.path).absolutePath())) { ++skipped; continue; }
        AuthorizedPath checked;
        if (!authorize(path.path, false, false, checked, code) || checked.path.compare(path.path, PathCase) != 0) { ++skipped; continue; }
        if (path.directory) {
            if (depth >= MaxDepth) { truncated = true; break; }
            QDirIterator children(path.path, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
            while (children.hasNext()) {
                if (pending.size() + visited >= MaxTraversalEntries) { truncated = true; break; }
                const QFileInfo child(children.next());
                pending.append({child.absoluteFilePath(), depth + 1});
            }
            continue;
        }
        if (scanned >= MaxSearchFiles || totalBytes >= MaxSearchBytes) { truncated = true; break; }
        ++scanned;
        const qint64 fileSize = QFileInfo(path.path).size();
        if (fileSize > MaxTextBytes) { ++skipped; continue; }
        if (fileSize > MaxSearchBytes - totalBytes) { truncated = true; break; }
        QByteArray bytes;
        QString content;
        const bool readable = readText(path.path, bytes, content, code, MaxSearchBytes - totalBytes < MaxTextBytes
                                       ? MaxSearchBytes - totalBytes : MaxTextBytes);
        totalBytes += bytes.size();
        if (!readable) { ++skipped; if (totalBytes > MaxSearchBytes) truncated = true; continue; }
        const QStringList lines = content.split(u'\n');
        for (qsizetype line = 0; line < lines.size(); ++line) {
            const qsizetype column = lines[line].indexOf(query, 0, sensitivity);
            if (column < 0) continue;
            if (matches.size() >= maximum) { truncated = true; break; }
            const qsizetype start = std::max<qsizetype>(0, column - 100);
            matches.append(QJsonObject{{QStringLiteral("path"), path.path}, {QStringLiteral("line"), static_cast<double>(line + 1)},
                                       {QStringLiteral("column"), static_cast<double>(column + 1)},
                                       {QStringLiteral("text"), lines[line].mid(start, 500)}});
        }
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("matches"), matches}, {QStringLiteral("truncated"), truncated},
            {QStringLiteral("files_scanned"), scanned}, {QStringLiteral("files_skipped"), skipped}};
}

bool WorkspaceAccess::backup(const QByteArray &bytes)
{
    const QString privateRoot = m_protectedRoots.first();
    if (!QDir().mkpath(m_backupRoot)) return false;
    const QString canonical = canonicalPath(m_backupRoot);
    if (canonical.isEmpty() || !within(normalized(canonical), resolvedRoot(privateRoot))) return false;
    DirectoryPins pins;
    if (!pins.acquire(normalized(canonical))) return false;
    QSaveFile file(normalized(canonical) + u'/' + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".bak"));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) return false;
    return file.write(bytes) == bytes.size() && file.commit();
}

QJsonObject WorkspaceAccess::write(const QJsonObject &args, bool replacing)
{
    const QStringList allowed = replacing
        ? QStringList{QStringLiteral("path"), QStringLiteral("old_text"), QStringLiteral("new_text"), QStringLiteral("expected_sha256")}
        : QStringList{QStringLiteral("path"), QStringLiteral("content"), QStringLiteral("expected_sha256")};
    const QStringList required = replacing
        ? QStringList{QStringLiteral("path"), QStringLiteral("old_text"), QStringLiteral("new_text"), QStringLiteral("expected_sha256")}
        : QStringList{QStringLiteral("path"), QStringLiteral("content")};
    if (!argumentsValid(args, allowed, required)
        || (args.contains(u"expected_sha256") && !args.value(u"expected_sha256").isString())
        || (replacing && args.value(u"old_text").toString().isEmpty()))
        return failure(QStringLiteral("invalid_arguments"));
    QString code;
    AuthorizedPath path;
    if (!authorize(args.value(u"path").toString(), true, !replacing, path, code)) return failure(code);
    if (path.directory) return failure(QStringLiteral("not_file"));
    const QString expected = args.value(u"expected_sha256").toString();
    if (path.exists && expected.isEmpty()) return failure(QStringLiteral("hash_required"));
    if (!path.exists && args.contains(u"expected_sha256")) return failure(QStringLiteral("not_found"));
    DirectoryPins pins;
    if (!pins.acquire(QFileInfo(path.path).absolutePath())) return failure(QStringLiteral("path_changed"));
    AuthorizedPath checked;
    if (!authorize(path.path, true, !replacing, checked, code)
        || checked.path.compare(path.path, PathCase) != 0 || checked.exists != path.exists)
        return failure(QStringLiteral("path_changed"));
    QByteArray original;
    QString originalText;
    if (path.exists) {
        if (!readText(path.path, original, originalText, code)) return failure(code);
        if (expected != hash(original) || m_readHashes.value(pathKey(path.path)) != expected)
            return failure(QStringLiteral("stale_hash"));
    }
    QString newText = args.value(u"content").toString();
    if (replacing) {
        const QString before = args.value(u"old_text").toString();
        const qsizetype position = originalText.indexOf(before);
        if (position < 0) return failure(QStringLiteral("replacement_not_found"));
        // Check overlapping occurrences too: replacing "aa" in "aaa" is ambiguous.
        if (originalText.indexOf(before, position + 1) >= 0) return failure(QStringLiteral("replacement_not_unique"));
        newText = originalText;
        newText.replace(position, before.size(), args.value(u"new_text").toString());
    }
    if (newText.size() > MaxTextBytes) return failure(QStringLiteral("too_large"));
    const QByteArray bytes = newText.toUtf8();
    QString validated;
    if (!textBytes(bytes, validated, code)) return failure(code);
    if (validated != newText) return failure(QStringLiteral("binary_file"));
    // A same-directory temporary file permits an atomic no-overwrite rename for
    // creates, and an atomic replacement rename for updates. QSaveFile would overwrite a
    // file created concurrently with a new-file operation.
    QString temporaryPath;
    {
        QTemporaryFile temporary(QFileInfo(path.path).absolutePath() + QStringLiteral("/.titan-save-XXXXXX"));
        if (!temporary.open() || !temporary.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
            || temporary.write(bytes) != bytes.size() || !temporary.flush())
            return failure(QStringLiteral("io_error"));
        temporaryPath = temporary.fileName();
        temporary.setAutoRemove(false);
        // Destroy QTemporaryFile to release its retained Windows native handle
        // before the replacement rename, which requires delete sharing.
    }
    struct TemporaryCleanup {
        QString path;
        ~TemporaryCleanup() { QFile::remove(path); }
    } cleanup{temporaryPath};
    if (path.exists) {
        if (!backup(original)) return failure(QStringLiteral("backup_failed"));
        QByteArray current;
        QString currentText;
        if (!authorize(path.path, true, false, checked, code) || checked.path.compare(path.path, PathCase) != 0)
            return failure(QStringLiteral("path_changed"));
        if (!readText(path.path, current, currentText, code) || hash(current) != expected)
            return failure(QStringLiteral("stale_hash"));
    }
    bool saved = false;
#ifdef Q_OS_WIN
    const QString destination = QDir::toNativeSeparators(path.path);
    const QString source = QDir::toNativeSeparators(temporaryPath);
    // Rename replaces the destination directory entry; it never follows a leaf
    // symbolic link. Its parent directories remain pinned throughout the call.
    saved = MoveFileExW(reinterpret_cast<LPCWSTR>(source.utf16()), reinterpret_cast<LPCWSTR>(destination.utf16()),
                        MOVEFILE_WRITE_THROUGH | (path.exists ? MOVEFILE_REPLACE_EXISTING : 0)) != FALSE;
#else
    if (!path.exists) saved = QFile::rename(temporaryPath, path.path);
    else {
        QSaveFile output(path.path);
        output.setDirectWriteFallback(false);
        saved = output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit();
    }
#endif
    if (!saved) return failure(!path.exists && QFileInfo::exists(path.path) ? QStringLiteral("already_exists") : QStringLiteral("io_error"));
    m_readHashes.remove(pathKey(path.path));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), path.path}, {QStringLiteral("sha256"), hash(bytes)},
            {QStringLiteral("bytes"), static_cast<double>(bytes.size())}, {QStringLiteral("created"), !path.exists},
            {QStringLiteral("backup_created"), path.exists}};
}

QJsonArray WorkspaceAccess::toolDefinitions()
{
    const auto definition = [](const QString &name, const QString &description, const QJsonObject &properties, const QJsonArray &required) {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("description"), description},
                {QStringLiteral("parameters"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                    {QStringLiteral("properties"), properties}, {QStringLiteral("required"), required},
                    {QStringLiteral("additionalProperties"), false}}}}}};
    };
    const auto path = stringProperty(QStringLiteral("Absolute path inside a user-granted folder. Traversal, device paths, and protected application data are forbidden."));
    const auto digest = stringProperty(QStringLiteral("Exact lowercase sha256 returned by files_read for this file since the latest grant change. Required to update an existing file."));
    const auto boolean = QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}};
    const auto count = [](int maximum) { return QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), maximum}}; };
    return {
        definition(QStringLiteral("files_list"), QStringLiteral("List granted directory entries. Optional recursion; at most 1000 returned entries, 5000 visited entries, depth 32. Protected paths and escaping links are omitted."),
            {{QStringLiteral("path"), path}, {QStringLiteral("recursive"), boolean}, {QStringLiteral("max_entries"), count(1000)}}, {QStringLiteral("path")}),
        definition(QStringLiteral("files_read"), QStringLiteral("Read one UTF-8 text file up to 1 MiB and return its content and sha256. Read the file before any update."),
            {{QStringLiteral("path"), path}}, {QStringLiteral("path")}),
        definition(QStringLiteral("files_search"), QStringLiteral("Search literal single-line text in a granted file or directory recursively. Returns bounded line excerpts; skips binary/oversize files. Limits: 500 matches, 1000 files, 16 MiB, depth 32, 5000 entries."),
            {{QStringLiteral("path"), path}, {QStringLiteral("query"), stringProperty(QStringLiteral("Nonempty literal single-line query, at most 4096 characters."))},
             {QStringLiteral("case_sensitive"), boolean}, {QStringLiteral("max_results"), count(500)}}, {QStringLiteral("path"), QStringLiteral("query")}),
        definition(QStringLiteral("files_write"), QStringLiteral("Atomically create or update a UTF-8 text file, at most 1 MiB, in a writable granted folder. Parent must exist. Omit expected_sha256 only for a new file; updates require a prior files_read hash and create a private backup."),
            {{QStringLiteral("path"), path}, {QStringLiteral("content"), stringProperty(QStringLiteral("Complete new UTF-8 text content."))}, {QStringLiteral("expected_sha256"), digest}},
            {QStringLiteral("path"), QStringLiteral("content")}),
        definition(QStringLiteral("files_replace"), QStringLiteral("Atomically replace exactly one literal occurrence, including overlapping matches, in a previously read UTF-8 file. Requires writable permission and a current read hash; creates a private backup."),
            {{QStringLiteral("path"), path}, {QStringLiteral("old_text"), stringProperty(QStringLiteral("Nonempty exact text occurring only once."))},
             {QStringLiteral("new_text"), stringProperty(QStringLiteral("Replacement text."))}, {QStringLiteral("expected_sha256"), digest}},
            {QStringLiteral("path"), QStringLiteral("old_text"), QStringLiteral("new_text"), QStringLiteral("expected_sha256")})
    };
}

} // namespace titan
