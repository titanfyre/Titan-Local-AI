#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <functional>
#include <atomic>
#include <memory>

class QFile;
class QLockFile;
class QNetworkReply;

namespace titan {
class RuntimeManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(QString baseUrl READ baseUrl NOTIFY stateChanged)
    Q_PROPERTY(QString logTail READ logTail NOTIFY stateChanged)
    Q_PROPERTY(QString modelId READ modelId NOTIFY stateChanged)
    Q_PROPERTY(bool supportsThinking READ supportsThinking NOTIFY capabilitiesChanged)
    Q_PROPERTY(QString thinkingStyle READ thinkingStyle NOTIFY capabilitiesChanged)
    Q_PROPERTY(QStringList thinkingLevels READ thinkingLevels NOTIFY capabilitiesChanged)
    Q_PROPERTY(QVariantList externalModels READ externalModels NOTIFY externalModelsChanged)
public:
    explicit RuntimeManager(QObject *parent = nullptr);
    ~RuntimeManager() override;
    void configure(const QVariantMap &settings);
    void loadModel(const QVariantMap &model);
    Q_INVOKABLE void ensureInstalled();
    void setExternalEndpoint(const QString &endpoint, const QString &model = {});
    Q_INVOKABLE void stop();
    void setExecutable(const QString &path);
    bool ready() const { return m_ready; }
    QString status() const { return m_status; }
    double progress() const { return m_progress; }
    QString baseUrl() const { return m_baseUrl; }
    QString logTail() const { return m_log; }
    bool supportsThinking() const { return m_thinking; }
    QString thinkingStyle() const { return m_thinkingStyle; }
    QStringList thinkingLevels() const { return m_thinkingLevels; }
    QString modelId() const { return m_modelId; }
    // Per-process local HTTP credential; deliberately not exposed to QML or logs.
    QString apiKey() const { return m_apiKey; }
    QVariantList externalModels() const { return m_externalModels; }
signals:
    void stateChanged();
    void modelReady(const QString &modelId);
    void failed(const QString &message);
    void capabilitiesChanged();
    void externalModelsChanged();
private:
    struct WorkResult { QString path; QString error; };
    void setStatus(const QString &, double progress = -1);
    void fail(const QString &);
    void appendLog(const QByteArray &);
    void setCapabilities(bool thinking, const QString &style = QStringLiteral("none"), const QStringList &levels = {});
    void inspectCapabilities(const QVariantMap &properties);
    void inspectProviderCapabilities(quint64 generation);
    void selectBackendAndInstall();
    void useBackend(const QString &);
    void downloadArchive();
    void verifyArchive();
    void extractArchive();
    void finishInstallation(const WorkResult &);
    void launchModel();
    void pollHealth();
    void stopOwnedProcess();
    void abortRequests();
    QNetworkReply *localRequest(const QString &url, const QByteArray &body = {});
    QString installDirectory() const;
    QVariantMap m_settings;
    QVariantMap m_model;
    QVariantList m_externalModels;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_download;
    QPointer<QNetworkReply> m_healthReply;
    QPointer<QProcess> m_process;
    QPointer<QProcess> m_extractor;
    QFutureWatcher<QString> m_backendProbe;
    QFutureWatcher<WorkResult> m_worker;
    std::function<void(const WorkResult &)> m_workFinished;
    std::shared_ptr<std::atomic_bool> m_workCancel = std::make_shared<std::atomic_bool>(false);
    std::unique_ptr<QFile> m_downloadFile;
    std::shared_ptr<QLockFile> m_installLock;
    QTimer m_healthTimer;
    QElapsedTimer m_loadTime;
    QString m_status = QStringLiteral("Choose a local GGUF model to begin");
    QString m_baseUrl;
    QString m_rootUrl;
    QString m_modelId;
    QString m_apiKey;
    QString m_log;
    QString m_executable;
    QString m_customExecutable;
    QString m_backend;
    QString m_archive;
    QString m_stage;
    QString m_thinkingStyle = QStringLiteral("none");
    QStringList m_thinkingLevels{QStringLiteral("auto")};
    double m_progress = -1;
    quint64 m_generation = 0;
    bool m_ready = false;
    bool m_thinking = false;
    bool m_installing = false;
    bool m_loadPending = false;
    bool m_external = false;
    bool m_cpuFallback = false;
};
} // namespace titan
