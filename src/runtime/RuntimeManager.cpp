#include "RuntimeManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QLockFile>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTcpServer>
#include <QThread>
#include <QUrl>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <utility>
#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace titan {
namespace {
// Official GitHub asset digests retrieved 2026-11-06 via
// https://api.github.com/repos/ggml-org/llama.cpp/releases/tags/b12167
// Pin updates require reviewing the official release and replacing both digests.
constexpr auto Release = "b12167";
struct RuntimeAsset { const char *backend; const char *name; const char *sha256; qint64 bytes; };
constexpr RuntimeAsset Assets[] {
    {"cpu", "llama-b12167-bin-win-cpu-x64.zip", "430d22ba7693f2135f9d3e5301f7d773cf1b8ba89fdf5ca3a5cdc5d5a7173980", 24726767},
    {"vulkan", "llama-b12167-bin-win-vulkan-x64.zip", "f99b2fc83d93f4a36778e5966600c8a4a36e8a3a6660521601317c02f974ca9d", 37685589}
};
const RuntimeAsset &assetFor(const QString &backend) { return Assets[backend == "vulkan" ? 1 : 0]; }
QString runtimeRoot() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/runtime";
}
void hideWindow(QProcess *process) {
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#else
    Q_UNUSED(process)
#endif
}
QByteArray hashFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return hash.result().toHex();
}
bool safeRelative(const QString &path) {
    if (path.isEmpty() || path.size() > 512 || QDir::isAbsolutePath(path)
        || path.contains(QLatin1Char(':')) || path.contains(QLatin1Char('\\'))) return false;
    const auto segments = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const auto &segment : segments)
        if (segment == ".." || segment == "." || segment.endsWith(' ') || segment.endsWith('.')) return false;
    return !segments.isEmpty();
}
QString findServer(const QString &directory) {
    QDirIterator files(directory, {"llama-server.exe"}, QDir::Files, QDirIterator::Subdirectories);
    if (!files.hasNext()) return {};
    return files.next();
}
QString validateInstalled(const QString &directory, const RuntimeAsset &asset) {
    QFile marker(directory + "/verified.json");
    if (!marker.open(QIODevice::ReadOnly) || marker.size() > 128 * 1024) return {};
    const auto object = QJsonDocument::fromJson(marker.readAll()).object();
    if (object.value("release").toString() != QLatin1String(Release)
        || object.value("archive_sha256").toString() != QLatin1String(asset.sha256)) return {};
    const auto hashes = object.value("files").toObject();
    if (hashes.isEmpty() || hashes.size() > 512) return {};
    qint64 total = 0;
    for (auto it = hashes.begin(); it != hashes.end(); ++it) {
        if (!safeRelative(it.key())) return {};
        const QFileInfo file(QDir(directory).filePath(it.key()));
        total += file.size();
        if (!file.isFile() || file.isSymLink() || file.size() > 512 * 1024 * 1024
            || total > 512 * 1024 * 1024
            || hashFile(file.absoluteFilePath()) != it.value().toString().toLatin1()) return {};
    }
    const auto server = findServer(directory);
    if (server.isEmpty() || !hashes.contains(QDir(directory).relativeFilePath(server))) return {};
    return server;
}
QString detectBackend() {
#ifdef Q_OS_WIN
    // Vulkan loader presence alone is insufficient: require a physical device.
    QLibrary vulkan(QStringLiteral("vulkan-1"));
    if (!vulkan.load()) return "cpu";
    using Instance = void *;
    struct ApplicationInfo { qint32 type; const void *next; const char *name; quint32 version;
                             const char *engine; quint32 engineVersion; quint32 apiVersion; };
    struct InstanceInfo { qint32 type; const void *next; quint32 flags; const ApplicationInfo *app;
                          quint32 layerCount; const char *const *layers; quint32 extensionCount; const char *const *extensions; };
    using Create = qint32(WINAPI *)(const InstanceInfo *, const void *, Instance *);
    using Enumerate = qint32(WINAPI *)(Instance, quint32 *, void *);
    using Destroy = void(WINAPI *)(Instance, const void *);
    const auto create = reinterpret_cast<Create>(vulkan.resolve("vkCreateInstance"));
    const auto enumerate = reinterpret_cast<Enumerate>(vulkan.resolve("vkEnumeratePhysicalDevices"));
    const auto destroy = reinterpret_cast<Destroy>(vulkan.resolve("vkDestroyInstance"));
    if (!create || !enumerate || !destroy) return "cpu";
    ApplicationInfo app{0, nullptr, "Titan Local AI", 1, "Titan", 1, (1u << 22)};
    InstanceInfo info{1, nullptr, 0, &app, 0, nullptr, 0, nullptr};
    Instance instance = nullptr;
    if (create(&info, nullptr, &instance) != 0 || !instance) return "cpu";
    quint32 count = 0;
    const auto result = enumerate(instance, &count, nullptr);
    destroy(instance, nullptr);
    return result == 0 && count > 0 ? "vulkan" : "cpu";
#else
    return "cpu";
#endif
}
int recommendedThreads() {
    const int logical = std::max(1, QThread::idealThreadCount());
#ifdef Q_OS_WIN
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    if (bytes > 0 && bytes <= 1024 * 1024) {
        QByteArray topology(int(bytes), Qt::Uninitialized);
        auto first = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(topology.data());
        if (GetLogicalProcessorInformationEx(RelationProcessorCore, first, &bytes)) {
            DWORD offset = 0;
            int physical = 0;
            while (offset + sizeof(LOGICAL_PROCESSOR_RELATIONSHIP) + sizeof(DWORD) <= bytes) {
                auto entry = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(topology.data() + offset);
                if (entry->Size == 0 || entry->Size > bytes - offset) break;
                if (entry->Relationship == RelationProcessorCore) ++physical;
                offset += entry->Size;
            }
            if (physical > 0) return std::min(physical, logical);
        }
    }
#endif
    return std::max(1, logical / 2);
}
bool templateThinks(const QString &text) {
    return text.contains("<think>") || text.contains("[THINK]") || text.contains("enable_thinking")
        || text.contains("reasoning_effort") || (text.contains("analysis") && text.contains("<|channel|>"));
}
bool explicitRemoteModel(const QString &id, const QJsonObject &metadata = {}) {
    // Official cloud tags include both :cloud and size-qualified tags such as :120b-cloud.
    static const QRegularExpression cloudTag(QStringLiteral("(?:^|[-_])cloud(?:$|[-_])"), QRegularExpression::CaseInsensitiveOption);
    const auto separator = id.lastIndexOf(':');
    const QString tag = separator > id.lastIndexOf('/') ? id.mid(separator + 1) : QString();
    return (!tag.isEmpty() && cloudTag.match(tag).hasMatch()) || !metadata.value("remote_host").toString().trimmed().isEmpty()
        || !metadata.value("remote_model").toString().trimmed().isEmpty();
}
QString endpointRoot(const QString &endpoint, QString *error) {
    QUrl url(endpoint.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https")
        || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()) {
        *error = "Use an HTTP(S) loopback endpoint without credentials, query, or fragment."; return {};
    }
    if (url.host().compare("localhost", Qt::CaseInsensitive) == 0) url.setHost("127.0.0.1");
    if (!QHostAddress(url.host()).isLoopback()) {
        *error = "Only local loopback providers are supported (127.0.0.1, localhost, or ::1)."; return {};
    }
    QString path = url.path();
    while (path.endsWith('/')) path.chop(1);
    if (path.endsWith("/v1")) path.chop(3);
    url.setPath(path);
    return url.toString(QUrl::FullyEncoded);
}
} // namespace

RuntimeManager::RuntimeManager(QObject *parent) : QObject(parent) {
    m_network.setProxy(QNetworkProxy::NoProxy);
    m_healthTimer.setInterval(400);
    connect(&m_healthTimer, &QTimer::timeout, this, &RuntimeManager::pollHealth);
    connect(&m_backendProbe, &QFutureWatcher<QString>::finished, this, [this] {
        if (m_installing) useBackend(m_backendProbe.result());
        else if (m_loadPending) QTimer::singleShot(0, this, &RuntimeManager::ensureInstalled);
    });
    connect(&m_worker, &QFutureWatcher<WorkResult>::finished, this, [this] {
        if (!m_installing) {
            m_workFinished = {}; m_installLock.reset();
            if (m_loadPending) QTimer::singleShot(0, this, &RuntimeManager::ensureInstalled);
            return;
        }
        if (m_installing && m_workFinished) {
            auto callback = std::move(m_workFinished);
            callback(m_worker.result());
        }
    });
}

RuntimeManager::~RuntimeManager() {
    m_workCancel->store(true);
    ++m_generation;
    m_healthTimer.stop();
    abortRequests();
    // stopOwnedProcess() retires processes asynchronously, so m_process alone
    // does not cover every owned child when the app closes. Reap all of them
    // before QObject destroys QProcess wrappers. kill() itself is asynchronous.
    const auto ownedProcesses = findChildren<QProcess *>(QString(), Qt::FindDirectChildrenOnly);
    for (auto process : ownedProcesses) {
        QObject::disconnect(process, nullptr, nullptr, nullptr);
        if (process->state() == QProcess::Starting) process->waitForStarted(1000);
        if (process->state() != QProcess::NotRunning) process->kill();
    }
    for (auto process : ownedProcesses)
        if (process->state() != QProcess::NotRunning) process->waitForFinished(3000);
    if (m_downloadFile) { m_downloadFile->close(); QFile::remove(m_downloadFile->fileName()); }
}

void RuntimeManager::configure(const QVariantMap &settings) { m_settings = settings; }

void RuntimeManager::setStatus(const QString &status, double progress) {
    m_status = status;
    m_progress = progress;
    emit stateChanged();
}

void RuntimeManager::appendLog(const QByteArray &data) {
    m_log += QString::fromUtf8(data);
    if (!m_apiKey.isEmpty()) m_log.replace(m_apiKey, QStringLiteral("[redacted]"));
    if (m_log.size() > 12000) m_log = m_log.right(12000);
}

void RuntimeManager::setCapabilities(bool thinking, const QString &style, const QStringList &levels) {
    QStringList supported{QStringLiteral("auto")};
    if (thinking) {
        for (const auto &level : levels)
            if (!supported.contains(level) && supported.size() < 24) supported << level;
    }
    const QString actualStyle = thinking ? style : QStringLiteral("none");
    if (m_thinking == thinking && m_thinkingStyle == actualStyle && m_thinkingLevels == supported) return;
    m_thinking = thinking;
    m_thinkingStyle = actualStyle;
    m_thinkingLevels = supported;
    emit capabilitiesChanged();
}

void RuntimeManager::fail(const QString &message) {
    m_workCancel->store(true);
    m_ready = false;
    m_installing = false;
    m_loadPending = false;
    m_healthTimer.stop();
    if (m_download) { m_download->disconnect(this); m_download->abort(); m_download->deleteLater(); m_download = nullptr; }
    if (m_downloadFile) { m_downloadFile->close(); QFile::remove(m_downloadFile->fileName()); m_downloadFile.reset(); }
    if (!m_worker.isRunning()) m_installLock.reset();
    stopOwnedProcess();
    setStatus(message);
    emit failed(message);
}

void RuntimeManager::setExecutable(const QString &path) {
    if (path.trimmed().isEmpty()) { m_customExecutable.clear(); m_executable.clear(); return; }
    const QFileInfo file(path);
    if (!file.isFile() || file.isSymLink()
#ifdef Q_OS_WIN
        || file.suffix().compare("exe", Qt::CaseInsensitive) != 0
#endif
    ) { fail("Select a local llama-server executable (.exe on Windows)."); return; }
    stop();
    m_customExecutable = file.canonicalFilePath();
    m_executable = m_customExecutable;
    setStatus("User-provided llama-server selected");
}

void RuntimeManager::loadModel(const QVariantMap &model) {
    const QFileInfo file(model.value("path").toString());
    QFile header(file.absoluteFilePath());
    if (!file.isFile() || file.suffix().compare("gguf", Qt::CaseInsensitive) != 0
        || !header.open(QIODevice::ReadOnly) || header.read(4) != QByteArrayLiteral("GGUF")) {
        fail("Managed inference requires a valid local GGUF model. Other formats need a compatible local provider."); return;
    }
    if (model.contains("loadable") && !model.value("loadable").toBool()) {
        fail(model.value("warning", "The split GGUF model is incomplete.").toString()); return;
    }
    if (!m_installing) ++m_generation;
    abortRequests();
    stopOwnedProcess();
    m_external = false;
    m_ready = false;
    m_baseUrl.clear();
    m_model = model;
    m_model.insert("path", file.absoluteFilePath());
    m_modelId = model.value("id", file.completeBaseName()).toString();
    m_cpuFallback = false;
    m_loadPending = true;
    m_log.clear();
    setCapabilities(model.value("supportsThinking", false).toBool());
    ensureInstalled();
}

QString RuntimeManager::installDirectory() const {
    return runtimeRoot() + '/' + QLatin1String(Release) + '-' + m_backend;
}

void RuntimeManager::ensureInstalled() {
    if (m_installing) return;
    if (m_worker.isRunning() || m_backendProbe.isRunning()) {
        setStatus("Finishing the previous runtime operation; try again shortly."); return;
    }
    if (!m_customExecutable.isEmpty()) {
        m_executable = m_customExecutable;
        if (m_loadPending) launchModel();
        else setStatus("User-provided llama-server is available", 1);
        return;
    }
#if !defined(Q_OS_WIN) || !defined(Q_PROCESSOR_X86_64)
    fail("Automatic runtime installation supports Windows x64. Choose your own llama-server executable on this platform.");
#else
    m_installing = true;
    m_workCancel = std::make_shared<std::atomic_bool>(false);
    selectBackendAndInstall();
#endif
}

void RuntimeManager::selectBackendAndInstall() {
    setStatus("Detecting local CPU and Vulkan devices");
    if (m_cpuFallback || m_settings.value("gpuLayers", -1).toInt() == 0) useBackend("cpu");
    else m_backendProbe.setFuture(QtConcurrent::run([] { return detectBackend(); }));
}

void RuntimeManager::useBackend(const QString &backend) {
    m_backend = backend;
    const QString directory = installDirectory();
    const RuntimeAsset asset = assetFor(backend);
    setStatus(QStringLiteral("Verifying cached %1 runtime").arg(backend));
    m_workFinished = [this](const WorkResult &result) {
        if (!result.path.isEmpty()) { finishInstallation(result); return; }
        if (!QDir().mkpath(runtimeRoot())) { fail("Cannot create the local runtime cache."); return; }
        m_installLock = std::make_shared<QLockFile>(runtimeRoot() + "/install.lock");
        m_installLock->setStaleLockTime(10 * 60 * 1000);
        if (!m_installLock->tryLock(0)) { fail("Another Titan instance is installing a runtime. Try again when it finishes."); return; }
        downloadArchive();
    };
    m_worker.setFuture(QtConcurrent::run([directory, asset] { return WorkResult{validateInstalled(directory, asset), {}}; }));
}

void RuntimeManager::downloadArchive() {
    const auto &asset = assetFor(m_backend);
    m_archive = runtimeRoot() + "/download-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".zip";
    m_downloadFile = std::make_unique<QFile>(m_archive);
    if (!m_downloadFile->open(QIODevice::WriteOnly | QIODevice::NewOnly)) { fail("Cannot create the runtime download file."); return; }
    const QString url = QStringLiteral("https://github.com/ggml-org/llama.cpp/releases/download/%1/%2")
        .arg(QLatin1String(Release), QLatin1String(asset.name));
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("User-Agent", "Titan-Local-AI/1.0");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(120000);
    m_download = m_network.get(request);
    setStatus(QStringLiteral("Downloading official llama.cpp %1 (%2)").arg(QLatin1String(Release), m_backend), 0);
    connect(m_download, &QIODevice::readyRead, this, [this, expected = asset.bytes] {
        if (!m_download || !m_downloadFile) return;
        const QByteArray chunk = m_download->readAll();
        if (m_downloadFile->size() + chunk.size() > expected || m_downloadFile->write(chunk) != chunk.size())
            fail("Runtime download exceeded its pinned size or could not be written to disk.");
    });
    connect(m_download, &QNetworkReply::downloadProgress, this, [this, expected = asset.bytes](qint64 received, qint64) {
        m_progress = std::clamp(double(received) / double(expected), 0.0, 1.0); emit stateChanged();
    });
    connect(m_download, &QNetworkReply::finished, this, [this, expected = asset.bytes] {
        auto reply = m_download;
        m_download = nullptr;
        if (!reply) return;
        const auto networkError = reply->error();
        const QString error = reply->errorString();
        if (m_downloadFile) {
            const QByteArray tail = reply->readAll();
            if (!tail.isEmpty() && m_downloadFile->write(tail) != tail.size()) {
                reply->deleteLater(); fail("Cannot finish writing the runtime archive."); return;
            }
            m_downloadFile->close(); m_downloadFile.reset();
        }
        reply->deleteLater();
        if (networkError != QNetworkReply::NoError) { QFile::remove(m_archive); fail("Runtime download failed: " + error); return; }
        if (QFileInfo(m_archive).size() != expected) { QFile::remove(m_archive); fail("Runtime archive size does not match the pinned official asset."); return; }
        verifyArchive();
    });
}

void RuntimeManager::verifyArchive() {
    setStatus("Verifying the runtime archive SHA-256");
    const QString archive = m_archive;
    const RuntimeAsset asset = assetFor(m_backend);
    m_workFinished = [this](const WorkResult &result) {
        if (!result.error.isEmpty()) { QFile::remove(m_archive); fail(result.error); return; }
        extractArchive();
    };
    m_worker.setFuture(QtConcurrent::run([archive, asset] {
        const QByteArray actual = hashFile(archive);
        if (actual != asset.sha256)
            return WorkResult{{}, QStringLiteral("SHA-256 verification failed for %1. Expected %2; received %3. The runtime was not installed.")
                              .arg(QLatin1String(asset.name), QLatin1String(asset.sha256), QString::fromLatin1(actual))};
        return WorkResult{archive, {}};
    }));
}

void RuntimeManager::extractArchive() {
    // Windows' bundled bsdtar extracts ZIP without any PowerShell helper or console.
    const QString tar = QStandardPaths::findExecutable("tar.exe", {qEnvironmentVariable("SystemRoot") + "/System32"});
    if (tar.isEmpty()) { fail("Windows tar.exe is unavailable. Select an existing llama-server executable instead."); return; }
    m_stage = runtimeRoot() + "/stage-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().mkpath(m_stage)) { fail("Cannot create the runtime staging directory."); return; }
    setStatus("Checking the verified runtime archive");
    auto process = new QProcess(this);
    m_extractor = process;
    hideWindow(process);
    process->setProgram(tar);
    process->setArguments({"-tf", m_archive});
    process->setProcessChannelMode(QProcess::SeparateChannels);
    auto listing = std::make_shared<QByteArray>();
    connect(process, &QProcess::readyReadStandardOutput, this, [process, listing] {
        *listing += process->readAllStandardOutput();
        if (listing->size() > 128 * 1024) process->kill();
    });
    const quint64 generation = m_generation;
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { process->deleteLater(); m_extractor = nullptr; fail("Cannot launch the Windows archive extractor."); }
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, listing, generation, tar](int code, QProcess::ExitStatus exitStatus) {
        *listing += process->readAllStandardOutput();
        const auto error = process->readAllStandardError();
        process->deleteLater(); m_extractor = nullptr;
        if (!m_installing || generation != m_generation) return;
        if (code != 0 || exitStatus != QProcess::NormalExit || listing->size() > 128 * 1024) {
            appendLog(error); fail("Cannot inspect the verified runtime archive."); return;
        }
        const auto entries = QString::fromUtf8(*listing).split('\n', Qt::SkipEmptyParts);
        if (entries.isEmpty() || entries.size() > 512) { fail("Runtime archive has an unexpected file count."); return; }
        for (auto name : entries) {
            name = name.trimmed();
            if (name.endsWith('/')) name.chop(1);
            if (!safeRelative(name)) { fail("Runtime archive contains an unsafe extraction path."); return; }
        }
        auto extraction = new QProcess(this);
        m_extractor = extraction;
        hideWindow(extraction);
        extraction->setProgram(tar);
        extraction->setArguments({"-xf", m_archive, "-C", m_stage});
        extraction->setProcessChannelMode(QProcess::MergedChannels);
        setStatus("Installing the verified runtime");
        connect(extraction, &QProcess::readyReadStandardOutput, this, [this, extraction] { appendLog(extraction->readAllStandardOutput()); });
        connect(extraction, &QProcess::errorOccurred, this, [this, extraction](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) { extraction->deleteLater(); m_extractor = nullptr; fail("Cannot start runtime archive extraction."); }
        });
        connect(extraction, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, extraction, generation](int exitCode, QProcess::ExitStatus status) {
            appendLog(extraction->readAllStandardOutput());
            extraction->deleteLater(); m_extractor = nullptr;
            if (!m_installing || generation != m_generation) return;
            if (exitCode != 0 || status != QProcess::NormalExit) { fail("Runtime archive extraction failed. " + m_log.right(1000)); return; }
            const QString stage = m_stage, destination = installDirectory(), archive = m_archive;
            const RuntimeAsset asset = assetFor(m_backend);
            const auto cancelled = m_workCancel;
            const auto installLock = m_installLock;
            m_workFinished = [this](const WorkResult &result) { finishInstallation(result); };
            m_worker.setFuture(QtConcurrent::run([stage, destination, archive, asset, cancelled, installLock] {
                Q_UNUSED(installLock) // Keep the cross-process lock until this worker finishes, including shutdown.
                QJsonObject hashes;
                QDirIterator files(stage, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                qint64 total = 0;
                while (files.hasNext()) {
                    if (cancelled->load()) return WorkResult{{}, "Runtime installation cancelled."};
                    const auto info = QFileInfo(files.next());
                    total += info.size();
                    if (info.isSymLink() || hashes.size() >= 512 || total > 512 * 1024 * 1024)
                        return WorkResult{{}, "Runtime extraction exceeded its bounds."};
                    const QByteArray hash = hashFile(info.absoluteFilePath());
                    if (hash.isEmpty()) return WorkResult{{}, "Cannot verify an extracted runtime file."};
                    hashes.insert(QDir(stage).relativeFilePath(info.absoluteFilePath()), QString::fromLatin1(hash));
                }
                const auto executable = findServer(stage);
                if (executable.isEmpty()) return WorkResult{{}, "The official archive does not contain llama-server.exe."};
                QSaveFile marker(stage + "/verified.json");
                const QJsonObject object{{"release", QLatin1String(Release)}, {"archive_sha256", QLatin1String(asset.sha256)}, {"files", hashes}};
                if (!marker.open(QIODevice::WriteOnly) || marker.write(QJsonDocument(object).toJson()) < 0 || !marker.commit())
                    return WorkResult{{}, "Cannot save the runtime verification record."};
                if (cancelled->load()) return WorkResult{{}, "Runtime installation cancelled."};
                // Replace a corrupt cache by renaming it aside; never extract over live binaries.
                QString retired;
                if (QFileInfo::exists(destination)) {
                    retired = destination + ".retired-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
                    if (!QDir().rename(destination, retired)) return WorkResult{{}, "Cannot replace the existing runtime cache. Close other Titan instances."};
                }
                if (!QDir().rename(stage, destination)) {
                    if (!retired.isEmpty()) QDir().rename(retired, destination);
                    return WorkResult{{}, "Cannot atomically activate the verified runtime."};
                }
                QFile::remove(archive);
                return WorkResult{QDir(destination).filePath(QDir(stage).relativeFilePath(executable)), {}};
            }));
        });
        extraction->start();
        QTimer::singleShot(120000, extraction, [extraction] { if (extraction->state() != QProcess::NotRunning) extraction->kill(); });
    });
    process->start();
    QTimer::singleShot(30000, process, [process] { if (process->state() != QProcess::NotRunning) process->kill(); });
}

void RuntimeManager::finishInstallation(const WorkResult &result) {
    m_installing = false;
    m_installLock.reset();
    if (!result.error.isEmpty() || result.path.isEmpty()) { fail(result.error.isEmpty() ? "Runtime installation failed." : result.error); return; }
    m_executable = result.path;
    setStatus(QStringLiteral("llama.cpp %1 ready (%2)").arg(QLatin1String(Release), m_backend), 1);
    if (m_loadPending) launchModel();
}

void RuntimeManager::launchModel() {
    if (m_model.isEmpty()) { fail("Choose a local GGUF model first."); return; }
    QTcpServer port;
    if (!port.listen(QHostAddress::LocalHost, 0)) { fail("Cannot reserve a local inference port."); return; }
    const quint16 number = port.serverPort();
    port.close();
    m_rootUrl = QStringLiteral("http://127.0.0.1:%1").arg(number);
    m_baseUrl = m_rootUrl + "/v1";
    m_apiKey = QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-')
        + QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');
    const int requestedThreads = m_settings.value("threads", 0).toInt();
    const int threads = std::clamp(requestedThreads > 0 ? requestedThreads : recommendedThreads(), 1, 256);
    const int context = std::clamp(m_settings.value("contextSize", 8192).toInt(), 512, 131072);
    const int configuredLayers = m_settings.value("gpuLayers", -1).toInt();
    const QString layers = m_cpuFallback || (m_customExecutable.isEmpty() && m_backend == "cpu") || configuredLayers == 0
        ? "0" : configuredLayers < 0 ? "auto" : QString::number(std::min(configuredLayers, 999));
    QStringList arguments{"--model", m_model.value("path").toString(), "--alias", m_modelId,
        "--host", "127.0.0.1", "--port", QString::number(number), "--ctx-size", QString::number(context),
        "--threads", QString::number(threads), "--n-gpu-layers", layers, "--parallel", "1", "--jinja", "--api-key", m_apiKey};
    if (m_customExecutable.isEmpty()) arguments << "--offline" << "--no-webui" << "--no-agent" << "--no-mmproj" << "--reasoning-format" << "auto";
    auto process = new QProcess(this);
    m_process = process;
    hideWindow(process);
    process->setProgram(m_executable);
    process->setArguments(arguments);
    process->setWorkingDirectory(QFileInfo(m_executable).absolutePath());
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto &key : environment.keys())
        if (key.startsWith("LLAMA_", Qt::CaseInsensitive) || key.startsWith("HF_", Qt::CaseInsensitive)) environment.remove(key);
    process->setProcessEnvironment(environment);
    process->setProcessChannelMode(QProcess::MergedChannels);
    const quint64 generation = m_generation;
    m_loadPending = false;
    m_ready = false;
    m_loadTime.start();
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] { appendLog(process->readAllStandardOutput()); });
    connect(process, &QProcess::started, this, [this, generation] {
        if (generation == m_generation) { m_healthTimer.start(); pollHealth(); }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, generation](QProcess::ProcessError error) {
        if (generation == m_generation && error == QProcess::FailedToStart)
            fail("Cannot start llama-server: " + process->errorString() + ". Install the Microsoft Visual C++ 2022 x64 Runtime if a DLL is missing.");
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, generation](int code, QProcess::ExitStatus) {
        appendLog(process->readAllStandardOutput());
        process->deleteLater();
        if (m_process == process) m_process = nullptr;
        if (generation != m_generation) return;
        m_healthTimer.stop();
        if (!m_ready && !m_cpuFallback && m_customExecutable.isEmpty() && m_backend == "vulkan"
            && m_settings.value("gpuLayers", -1).toInt() < 0) {
            m_cpuFallback = true; m_loadPending = true;
            appendLog("\nVulkan startup failed; retrying with the verified CPU runtime.\n");
            ensureInstalled(); return;
        }
        fail(QStringLiteral("llama-server exited (%1). %2").arg(code).arg(m_log.right(2000)));
    });
    setStatus(QStringLiteral("Loading %1 (%2)").arg(m_model.value("name").toString(), layers == "0" ? "CPU" : "GPU automatic"));
    process->start();
}

QNetworkReply *RuntimeManager::localRequest(const QString &url, const QByteArray &body) {
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(5000);
    request.setRawHeader("Accept", "application/json");
    if (!m_external && !m_apiKey.isEmpty()) request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());
    QNetworkReply *reply;
    if (body.isEmpty()) reply = m_network.get(request);
    else { request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"); reply = m_network.post(request, body); }
    // Bound non-streaming discovery/health responses from arbitrary local providers.
    connect(reply, &QIODevice::readyRead, reply, [reply] { if (reply->bytesAvailable() > 4 * 1024 * 1024) reply->abort(); });
    return reply;
}

void RuntimeManager::pollHealth() {
    if (m_external || m_ready || !m_process) { m_healthTimer.stop(); return; }
    if (m_loadTime.elapsed() > 180000) {
        fail("The local model did not become ready within 180 seconds. Try a smaller model or context. " + m_log.right(2000)); return;
    }
    if (m_healthReply) return;
    const quint64 generation = m_generation;
    auto reply = localRequest(m_rootUrl + "/health");
    m_healthReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto object = QJsonDocument::fromJson(reply->readAll()).object();
        const bool success = reply->error() == QNetworkReply::NoError && status == 200 && object.value("status").toString() == "ok";
        reply->deleteLater();
        if (m_healthReply == reply) m_healthReply = nullptr;
        if (generation != m_generation || !success || !m_process || m_ready) return;
        m_ready = true;
        m_healthTimer.stop();
        setStatus("Local model ready", 1);
        inspectProviderCapabilities(generation);
        emit modelReady(m_modelId);
    });
}

void RuntimeManager::inspectCapabilities(const QVariantMap &properties) {
    const QString templ = properties.value("chat_template").toString();
    const auto caps = properties.value("chat_template_caps").toMap();
    const auto capabilities = properties.value("capabilities").toList();
    bool thinking = m_thinking || templateThinks(templ);
    QString style = m_thinkingStyle;
    QStringList levels = m_thinkingLevels;
    if (templ.contains("enable_thinking")) {
        thinking = true; style = "enable_thinking"; levels = {"auto", "on", "off"};
    }
    if (caps.value("supports_reasoning_effort").toBool() || !caps.value("reasoning_effort_levels").toList().isEmpty()
        || templ.contains("reasoning_effort")) {
        thinking = true; style = "reasoning_effort";
        levels = {"auto"};
        const auto advertised = caps.value("reasoning_effort_levels").toList();
        if (!advertised.isEmpty()) {
            for (const auto &value : advertised) {
                const QString level = value.toString();
                if (level == "none") levels << "off";
                else if (level == "low" || level == "medium" || level == "high") levels << level;
            }
        } else levels << "low" << "medium" << "high";
    }
    for (const auto &capability : capabilities)
        if (capability.toString() == "thinking") {
            thinking = true;
            // Capability evidence establishes thinking, but cannot replace explicit effort controls.
            if (style == "none") style = "ollama";
        }
    // Current Ollama descriptors are authoritative, including explicit lack of support.
    const auto descriptor = properties.value("thinking").toMap();
    if (descriptor.contains("values")) {
        const auto values = descriptor.value("values").toList();
        bool on = false, off = false, namedThinking = false;
        QStringList named;
        for (const auto &value : values) {
            if (value.metaType().id() == QMetaType::Bool) {
                if (value.toBool()) on = true;
                else off = true;
            } else if (value.metaType().id() == QMetaType::QString) {
                const QString name = value.toString();
                namedThinking = namedThinking || !name.isEmpty();
                // These are the efforts represented by the application's settings contract.
                if ((name == "low" || name == "medium" || name == "high") && !named.contains(name)) named << name;
            }
        }
        thinking = on || namedThinking;
        levels = {"auto"};
        if (namedThinking) {
            style = "reasoning_effort";
            levels << named;
            if (on) levels << "on";
            if (off) levels << "off";
        } else if (on) {
            style = "ollama";
            levels << "on";
            if (off) levels << "off";
        } else style = "none";
    }
    setCapabilities(thinking, style, levels);
}

void RuntimeManager::inspectProviderCapabilities(quint64 generation) {
    if (m_external && m_settings.value("provider").toString() == "ollama") {
        const auto body = QJsonDocument(QJsonObject{{"model", m_modelId}, {"verbose", false}}).toJson(QJsonDocument::Compact);
        auto show = localRequest(m_rootUrl + "/api/show", body);
        connect(show, &QNetworkReply::finished, this, [this, show, generation] {
            const auto document = QJsonDocument::fromJson(show->readAll());
            auto object = document.object();
            const bool success = show->error() == QNetworkReply::NoError && document.isObject()
                && show->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
            show->deleteLater();
            if (generation != m_generation) return;
            if (!success) { fail("Cannot verify the selected Ollama model's local metadata. Check that the model is installed locally."); return; }
            if (explicitRemoteModel(m_modelId, object)) {
                for (qsizetype index = m_externalModels.size(); index-- > 0;)
                    if (m_externalModels[index].toMap().value("id").toString() == m_modelId) m_externalModels.removeAt(index);
                emit externalModelsChanged();
                setCapabilities(false);
                fail("This Ollama model uses a remote host. Choose a model installed locally or use managed GGUF inference."); return;
            }
            object.insert("chat_template", object.value("template"));
            inspectCapabilities(object.toVariantMap());
            for (auto &value : m_externalModels) {
                auto entry = value.toMap();
                if (entry.value("id").toString() != m_modelId) continue;
                entry.insert("supportsThinking", m_thinking);
                value = entry;
            }
            emit externalModelsChanged();
            m_ready = true;
            setStatus("Local Ollama model connected", 1);
            emit modelReady(m_modelId);
        });
        return;
    }
    auto reply = localRequest(m_rootUrl + "/props");
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        const auto object = QJsonDocument::fromJson(reply->readAll()).object();
        const bool success = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        if (generation == m_generation && success) inspectCapabilities(object.toVariantMap());
    });
}

void RuntimeManager::setExternalEndpoint(const QString &endpoint, const QString &model) {
    QString error;
    const auto root = endpointRoot(endpoint, &error);
    if (root.isEmpty()) { fail(error); return; }
    stop();
    m_external = true;
    m_rootUrl = root;
    m_baseUrl = root + "/v1";
    m_modelId = model.trimmed();
    m_externalModels.clear(); emit externalModelsChanged();
    const quint64 generation = m_generation;
    const bool ollama = m_settings.value("provider").toString() == "ollama";
    if (ollama && explicitRemoteModel(m_modelId)) {
        fail("Ollama cloud models are unavailable in local mode. Choose a model installed locally."); return;
    }
    setStatus("Connecting to local provider");
    auto reply = localRequest(ollama ? m_rootUrl + "/api/tags" : m_baseUrl + "/models");
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, ollama] {
        const auto document = QJsonDocument::fromJson(reply->readAll());
        const auto object = document.object();
        const bool success = reply->error() == QNetworkReply::NoError && document.isObject()
            && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
            && object.value(ollama ? "models" : "data").isArray();
        const QString error = reply->errorString();
        reply->deleteLater();
        if (generation != m_generation) return;
        if (!success) { fail("Cannot connect to the local provider: " + error); return; }
        const auto data = object.value(ollama ? "models" : "data").toArray();
        bool selectedRemote = false;
        for (const auto &value : data) {
            const auto entry = value.toObject();
            const QString id = ollama ? entry.value("model").toString(entry.value("name").toString()) : entry.value("id").toString();
            if (ollama && explicitRemoteModel(id, entry)) {
                if (id == m_modelId || entry.value("name").toString() == m_modelId) selectedRemote = true;
                continue;
            }
            if (id.isEmpty() || m_externalModels.size() >= 512) continue;
            m_externalModels << QVariantMap{{"id", id}, {"name", entry.value("name").toString(id)}, {"path", ""},
                {"sizeLabel", "Local provider"}, {"quantization", "Provider"}, {"architecture", "Provider"}, {"supportsThinking", false}};
        }
        emit externalModelsChanged();
        if (selectedRemote) { fail("The selected Ollama model uses a remote host. Choose a model installed locally."); return; }
        if (m_modelId.isEmpty() && !m_externalModels.isEmpty()) m_modelId = m_externalModels.first().toMap().value("id").toString();
        if (m_modelId.isEmpty()) { fail("The local provider has no available model. Load a model in the provider, then connect again."); return; }
        if (ollama || !m_externalModels.isEmpty()) {
            bool exists = false;
            for (const auto &entry : m_externalModels) if (entry.toMap().value("id").toString() == m_modelId) exists = true;
            if (!exists) { fail("The selected model is not listed by this local provider. Choose an available model ID."); return; }
        }
        if (ollama) { inspectProviderCapabilities(generation); return; }
        m_ready = true;
        setStatus("Local provider connected", 1);
        inspectProviderCapabilities(generation);
        emit modelReady(m_modelId);
    });
}

void RuntimeManager::stopOwnedProcess() {
    auto process = m_process;
    m_process = nullptr;
    if (!process) return;
    process->disconnect(this);
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process, &QObject::deleteLater);
    if (process->state() == QProcess::NotRunning) { process->deleteLater(); return; }
    process->terminate();
    QTimer::singleShot(1500, process, [process] { if (process->state() != QProcess::NotRunning) process->kill(); });
}

void RuntimeManager::abortRequests() {
    // Only this manager's discovery requests; never stop a separate provider process.
    const auto replies = m_network.findChildren<QNetworkReply *>();
    for (auto reply : replies) {
        if (reply == m_download) continue;
        reply->disconnect(this); reply->abort(); reply->deleteLater();
    }
    m_healthReply = nullptr;
}

void RuntimeManager::stop() {
    m_workCancel->store(true);
    ++m_generation;
    m_healthTimer.stop();
    abortRequests();
    if (m_download) { m_download->disconnect(this); m_download->abort(); m_download->deleteLater(); m_download = nullptr; }
    if (m_downloadFile) { m_downloadFile->close(); QFile::remove(m_downloadFile->fileName()); m_downloadFile.reset(); }
    if (m_extractor) { m_extractor->disconnect(this); m_extractor->kill(); m_extractor->deleteLater(); m_extractor = nullptr; }
    m_installing = false;
    m_loadPending = false;
    if (!m_worker.isRunning()) m_installLock.reset();
    stopOwnedProcess();
    m_ready = false;
    m_baseUrl.clear();
    m_apiKey.clear();
    m_rootUrl.clear();
    m_modelId.clear();
    if (!m_externalModels.isEmpty()) {
        m_externalModels.clear();
        emit externalModelsChanged();
    }
    setCapabilities(false);
    setStatus("Local runtime stopped");
}
} // namespace titan
