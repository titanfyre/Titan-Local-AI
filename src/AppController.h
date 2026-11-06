#pragma once
#include "AgentEngine.h"
#include "SettingsStore.h"
#include "runtime/ModelCatalog.h"
#include "runtime/RuntimeManager.h"
#include <QTimer>
namespace titan {
class AppController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractListModel *messages READ messages CONSTANT)
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(QVariantList folderGrants READ folderGrants NOTIFY folderGrantsChanged)
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap systemInfo READ systemInfo CONSTANT)
    Q_PROPERTY(QString selectedModelId READ selectedModelId NOTIFY selectedModelChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
    Q_PROPERTY(QString runtimeStatus READ runtimeStatus NOTIFY runtimeChanged)
    Q_PROPERTY(bool runtimeReady READ runtimeReady NOTIFY runtimeChanged)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY runtimeChanged)
    Q_PROPERTY(bool browserConnected READ browserConnected NOTIFY browserChanged)
    Q_PROPERTY(QString modelsPath READ modelsPath NOTIFY modelsPathChanged)
    Q_PROPERTY(bool thinkingSupported READ thinkingSupported NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap pendingApproval READ pendingApproval NOTIFY approvalChanged)
  public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;
    QAbstractListModel *messages() { return &chat_; }
    QVariantList models() const;
    QVariantList folderGrants() const;
    QVariantList sessions() const { return sessions_; }
    QVariantMap settings() const;
    QVariantMap systemInfo() const { return info_; }
    QString selectedModelId() const { return selectedModel_; }
    bool busy() const { return agent_.busy(); }
    QString status() const { return agent_.busy() ? agent_.status() : QStringLiteral("Prêt"); }
    QString errorMessage() const { return error_; }
    QString runtimeStatus() const;
    bool runtimeReady() const { return runtime_.ready(); }
    double downloadProgress() const { return runtime_.progress(); }
    bool browserConnected() const { return tools_.browserConnected(); }
    QString modelsPath() const { return catalog_.directory(); }
    bool thinkingSupported() const { return runtime_.supportsThinking(); }
    QVariantMap pendingApproval() const { return approval_; }
    Q_INVOKABLE void sendPrompt(const QString &prompt);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void newChat();
    Q_INVOKABLE void openSession(const QString &id);
    Q_INVOKABLE void deleteSession(const QString &id);
    Q_INVOKABLE void chooseModelsFolder();
    Q_INVOKABLE void openModelsFolder();
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE void chooseGrantFolder();
    Q_INVOKABLE void removeGrant(const QString &path);
    Q_INVOKABLE void setGrantWritable(const QString &path, bool writable);
    Q_INVOKABLE void selectModel(const QString &id);
    Q_INVOKABLE void updateSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void installRuntime();
    Q_INVOKABLE void connectProvider();
    Q_INVOKABLE void openBrowser();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void answerApproval(bool allowed);
    Q_INVOKABLE void chooseRuntimeExecutable();
    // Used by executable smoke/integration checks; never exposed as an agent tool.
    bool runWorkspaceSmoke(const QString &path);
  signals:
    void modelsChanged();
    void folderGrantsChanged();
    void sessionsChanged();
    void settingsChanged();
    void selectedModelChanged();
    void stateChanged();
    void errorChanged();
    void runtimeChanged();
    void browserChanged();
    void modelsPathChanged();
    void approvalChanged();

  private:
    SettingsStore store_;
    ChatModel chat_;
    ModelCatalog catalog_;
    RuntimeManager runtime_;
    ToolBroker tools_;
    AgentEngine agent_;
    QVariantList sessions_;
    QVariantMap info_, approval_;
    QString selectedModel_, sessionId_, sessionTitle_, error_;
    QTimer saveTimer_, settingsTimer_, reloadTimer_;
    bool openingBrowser_ = false;
    void error(const QString &text);
    void saveSession();
    void saveGrants(const QList<FolderGrant> &grants);
    QVariantMap selectedModel() const;
    bool managed() const { return store_.value("provider").toString() == "managed"; }
};
} // namespace titan
