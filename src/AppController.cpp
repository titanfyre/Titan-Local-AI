#include "AppController.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSysInfo>
#include <QThread>
#include <QUuid>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace titan {
AppController::AppController(QObject *p)
    : QObject(p), chat_(this), catalog_(this), runtime_(this), tools_(store_.dataPath(), this),
      agent_(&chat_, &tools_, this) {
    info_ = {{"os", QSysInfo::prettyProductName()},
             {"architecture", QSysInfo::currentCpuArchitecture()},
             {"threads", QThread::idealThreadCount()},
             {"cpu", QString("%1 threads logiques").arg(QThread::idealThreadCount())}};
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        info_["ramGiB"] = double(memory.ullTotalPhys) / double(1024LL * 1024 * 1024);
        info_["memory"] =
            QString::number(double(memory.ullTotalPhys) / double(1024LL * 1024 * 1024), 'f', 1) + " Go RAM";
    }
#endif
    sessions_ = store_.sessions();
    QList<FolderGrant> grants;
    for (const auto &v : store_.grants()) {
        auto g = v.toObject();
        if (QDir(g.value("path").toString()).exists())
            grants.append({g.value("path").toString(), g.value("writable").toBool()});
    }
    tools_.setGrants(grants);
    connect(&catalog_, &ModelCatalog::modelsChanged, this, [this] {
        emit modelsChanged();
        if (managed() && selectedModel_.isEmpty()) {
            const auto previous = store_.value("lastModelId").toString();
            for (const auto &v : catalog_.models())
                if (!previous.isEmpty() && v.toMap().value("id").toString() == previous)
                    selectModel(previous);
        }
    });
    connect(&catalog_, &ModelCatalog::directoryChanged, this, &AppController::modelsPathChanged);
    connect(&catalog_, &ModelCatalog::scanningChanged, this, [this] {
        if (!catalog_.scanWarning().isEmpty())
            error(catalog_.scanWarning());
    });
    connect(&runtime_, &RuntimeManager::stateChanged, this, &AppController::runtimeChanged);
    connect(&runtime_, &RuntimeManager::failed, this, &AppController::error);
    connect(&runtime_, &RuntimeManager::capabilitiesChanged, this, &AppController::settingsChanged);
    connect(&runtime_, &RuntimeManager::externalModelsChanged, this, &AppController::modelsChanged);
    connect(&runtime_, &RuntimeManager::modelReady, this, [this](const QString &id) {
        selectedModel_ = id;
        emit selectedModelChanged();
        emit runtimeChanged();
        emit settingsChanged();
    });
    connect(&agent_, &AgentEngine::changed, this, &AppController::stateChanged);
    connect(&agent_, &AgentEngine::error, this, &AppController::error);
    connect(&agent_, &AgentEngine::finished, this, [this] {
        saveSession();
        emit stateChanged();
    });
    connect(&agent_, &AgentEngine::approvalRequested, this, [this](const QVariantMap &value) {
        approval_ = value;
        emit approvalChanged();
    });
    connect(&tools_, &ToolBroker::browserStateChanged, this, &AppController::browserChanged);
    connect(&tools_, &ToolBroker::completed, this, [this](const QString &id, const QJsonObject &result) {
        if (id == "ui_open_browser") {
            openingBrowser_ = false;
            if (!result.value("ok").toBool())
                error(result.value("error").toObject().value("message").toString());
            emit browserChanged();
        }
    });
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(1200);
    connect(&saveTimer_, &QTimer::timeout, this, &AppController::saveSession);
    connect(&chat_, &QAbstractItemModel::dataChanged, this, [this] { saveTimer_.start(); });
    settingsTimer_.setSingleShot(true);
    settingsTimer_.setInterval(250);
    connect(&settingsTimer_, &QTimer::timeout, this, [this] { store_.save(); });
    reloadTimer_.setSingleShot(true);
    reloadTimer_.setInterval(500);
    connect(&reloadTimer_, &QTimer::timeout, this, [this] {
        if (managed() && !busy() && !selectedModel_.isEmpty()) {
            runtime_.configure(store_.settings());
            runtime_.loadModel(selectedModel());
        }
    });
    tools_.protectModels(store_.modelsDirectory());
    runtime_.configure(store_.settings());
    catalog_.setDirectory(store_.modelsDirectory());
    newChat();
    if (!managed())
        QTimer::singleShot(0, this, &AppController::connectProvider);
}
AppController::~AppController() {
    agent_.cancel();
    saveSession();
    store_.save();
}
QVariantList AppController::models() const {
    return managed() ? catalog_.models() : runtime_.externalModels();
}
QVariantMap AppController::settings() const {
    auto s = store_.settings();
    auto levels = runtime_.thinkingLevels();
    s["availableThinkingLevels"] = levels;
    if (!levels.contains(s.value("thinkingLevel").toString()))
        s["thinkingLevel"] = "auto";
    return s;
}
QVariantList AppController::folderGrants() const {
    QVariantList list;
    for (const auto &g : tools_.grants())
        list.append(QVariantMap{{"path", g.path}, {"writable", g.writable}});
    return list;
}
QString AppController::runtimeStatus() const {
    auto s = runtime_.status();
    if (s == "Local model ready")
        return "Modèle local prêt";
    if (s == "Local provider connected")
        return "Service local connecté";
    if (s.startsWith("Choose a local"))
        return "Choisissez un modèle local pour commencer";
    if (s == "Connecting to local provider")
        return "Connexion au service local…";
    return s;
}
void AppController::error(const QString &text) {
    error_ = text;
    emit errorChanged();
}
void AppController::clearError() {
    error_.clear();
    emit errorChanged();
}
QVariantMap AppController::selectedModel() const {
    for (const auto &v : models()) {
        auto m = v.toMap();
        if (m.value("id").toString() == selectedModel_)
            return m;
    }
    return {};
}
void AppController::sendPrompt(const QString &prompt) {
    if (busy() || prompt.trimmed().isEmpty())
        return;
    if (!runtime_.ready()) {
        error("Choisissez et chargez un modèle local avant d’envoyer un message.");
        return;
    }
    clearError();
    if (sessionTitle_.isEmpty())
        sessionTitle_ = prompt.simplified().left(70);
    chat_.add("user", prompt);
    agent_.start(prompt, settings(), runtime_.modelId(), QUrl(runtime_.baseUrl()), runtime_.apiKey(),
                 runtime_.thinkingStyle());
    saveTimer_.start();
}
void AppController::stop() {
    agent_.cancel();
}
void AppController::newChat() {
    if (busy()) {
        error("Arrêtez la génération avant de changer de conversation.");
        return;
    }
    saveSession();
    chat_.clear();
    agent_.setHistory({});
    sessionId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    sessionTitle_.clear();
    clearError();
}
void AppController::saveSession() {
    if (sessionId_.isEmpty() || chat_.rowCount() == 0)
        return;
    store_.saveSession(sessionId_, sessionTitle_.isEmpty() ? "Conversation" : sessionTitle_, chat_.toJson(),
                       agent_.history());
    sessions_ = store_.sessions();
    emit sessionsChanged();
}
void AppController::openSession(const QString &id) {
    if (busy()) {
        error("Arrêtez la génération avant de changer de conversation.");
        return;
    }
    saveSession();
    auto o = store_.loadSession(id);
    if (o.isEmpty())
        return;
    sessionId_ = id;
    sessionTitle_ = o.value("title").toString();
    chat_.fromJson(o.value("messages").toArray());
    agent_.setHistory(o.value("history").toArray());
    clearError();
}
void AppController::deleteSession(const QString &id) {
    if (busy())
        return;
    store_.deleteSession(id);
    if (id == sessionId_) {
        chat_.clear();
        agent_.setHistory({});
        sessionId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
        sessionTitle_.clear();
    }
    sessions_ = store_.sessions();
    emit sessionsChanged();
}
void AppController::chooseModelsFolder() {
    if (busy())
        return;
    auto path = QFileDialog::getExistingDirectory(nullptr, "Dossier des modèles GGUF", catalog_.directory());
    if (path.isEmpty())
        return;
    runtime_.stop();
    selectedModel_.clear();
    store_.setModelsDirectory(path);
    tools_.protectModels(path);
    saveGrants(tools_.grants());
    catalog_.setDirectory(path);
    emit selectedModelChanged();
    emit modelsPathChanged();
}
void AppController::openModelsFolder() {
    QDir().mkpath(catalog_.directory());
    QDesktopServices::openUrl(QUrl::fromLocalFile(catalog_.directory()));
}
void AppController::refreshModels() {
    if (managed())
        catalog_.refresh();
    else
        connectProvider();
}
void AppController::chooseGrantFolder() {
    auto path = QFileDialog::getExistingDirectory(nullptr, "Autoriser un dossier en lecture");
    if (path.isEmpty())
        return;
    path = QDir::cleanPath(QFileInfo(path).canonicalFilePath());
    auto g = tools_.grants();
    for (const auto &v : g)
        if (v.path.compare(path, Qt::CaseInsensitive) == 0)
            return;
    g.append({path, false});
    saveGrants(g);
}
void AppController::saveGrants(const QList<FolderGrant> &grants) {
    tools_.setGrants(grants);
    QJsonArray a;
    for (const auto &g : tools_.grants())
        a.append(QJsonObject{{"path", g.path}, {"writable", g.writable}});
    store_.setGrants(a);
    emit folderGrantsChanged();
}
void AppController::removeGrant(const QString &path) {
    auto g = tools_.grants();
    g.removeIf([&](const FolderGrant &v) { return v.path == path; });
    saveGrants(g);
}
void AppController::setGrantWritable(const QString &path, bool writable) {
    auto g = tools_.grants();
    for (auto &v : g)
        if (v.path == path)
            v.writable = writable;
    saveGrants(g);
}
void AppController::selectModel(const QString &id) {
    if (busy()) {
        error("Arrêtez la génération avant de changer de modèle.");
        return;
    }
    QVariantMap found;
    for (const auto &v : models())
        if (v.toMap().value("id").toString() == id)
            found = v.toMap();
    if (found.isEmpty())
        return;
    if (found.contains("loadable") && !found.value("loadable").toBool()) {
        error(found.value("warning", "Modèle incomplet.").toString());
        return;
    }
    clearError();
    selectedModel_ = id;
    emit selectedModelChanged();
    runtime_.configure(store_.settings());
    if (managed()) {
        store_.update("lastModelId", id);
        store_.save();
        runtime_.loadModel(found);
    } else {
        store_.update("externalModel", id);
        store_.save();
        runtime_.setExternalEndpoint(store_.value("endpoint").toString(), id);
    }
    emit settingsChanged();
}
void AppController::updateSetting(const QString &key, const QVariant &value) {
    if (busy() &&
        QStringList{"provider", "endpoint", "externalModel", "contextSize", "threads", "gpuLayers"}.contains(
            key)) {
        error("Arrêtez la génération avant de modifier le moteur.");
        emit settingsChanged();
        return;
    }
    if (!store_.update(key, value))
        return;
    settingsTimer_.start();
    if (QStringList{"provider", "endpoint", "externalModel"}.contains(key)) {
        reloadTimer_.stop();
        runtime_.stop();
        selectedModel_.clear();
        if (key == "provider" && value.toString() == "ollama")
            store_.update("endpoint", "http://127.0.0.1:11434/v1");
        else if (key == "provider" && value.toString() == "lmstudio")
            store_.update("endpoint", "http://127.0.0.1:1234/v1");
        emit modelsChanged();
        emit selectedModelChanged();
    }
    runtime_.configure(store_.settings());
    if (QStringList{"contextSize", "threads", "gpuLayers"}.contains(key) && managed() &&
        !selectedModel_.isEmpty()) {
        runtime_.stop();
        reloadTimer_.start();
    }
    emit settingsChanged();
}
void AppController::installRuntime() {
    if (busy())
        return;
    runtime_.configure(store_.settings());
    runtime_.ensureInstalled();
}
void AppController::connectProvider() {
    if (busy())
        return;
    clearError();
    runtime_.configure(store_.settings());
    if (managed()) {
        if (!selectedModel_.isEmpty())
            runtime_.loadModel(selectedModel());
        else
            error("Choisissez un fichier GGUF dans la liste des modèles.");
    } else
        runtime_.setExternalEndpoint(store_.value("endpoint").toString(),
                                     store_.value("externalModel").toString());
}
void AppController::openBrowser() {
    if (openingBrowser_)
        return;
    openingBrowser_ = true;
    tools_.execute("ui_open_browser", "browser_open", {});
}
void AppController::answerApproval(bool allowed) {
    approval_.clear();
    emit approvalChanged();
    agent_.approve(allowed);
}
void AppController::chooseRuntimeExecutable() {
    if (busy())
        return;
    auto path = QFileDialog::getOpenFileName(nullptr, "Choisir llama-server.exe", {},
                                             "llama-server (llama-server.exe);;Exécutables (*.exe)");
    if (!path.isEmpty()) {
        runtime_.setExecutable(path);
        connectProvider();
    }
}
bool AppController::runWorkspaceSmoke(const QString &path) {
    WorkspaceAccess access({store_.dataPath()});
    access.setGrants({{path, true}});
    auto created = access.invoke("files_write",
                                 {{"path", path + "/titan-smoke.txt"}, {"content", "Titan Local AI smoke"}});
    if (!created.value("ok").toBool())
        return false;
    auto read = access.invoke("files_read", {{"path", path + "/titan-smoke.txt"}});
    return read.value("content").toString() == "Titan Local AI smoke";
}
} // namespace titan
