#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QHash>
#include <QTimer>
#include <QVariantList>
#include <atomic>
#include <memory>

namespace titan {

// Lists GGUF model metadata only. Discovery never loads tensor data into memory.
class ModelCatalog final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString directory READ directory WRITE setDirectory NOTIFY directoryChanged)
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QString scanWarning READ scanWarning NOTIFY scanningChanged)
public:
    explicit ModelCatalog(QObject *parent = nullptr);
    ~ModelCatalog() override;
    void setDirectory(const QString &directory);
    QString directory() const { return m_directory; }
    Q_INVOKABLE void refresh();
    QVariantList models() const { return m_models; }
    bool scanning() const { return m_scanning; }
    QString scanWarning() const { return m_warning; }
signals:
    void modelsChanged();
    void directoryChanged();
    void scanningChanged();
private:
    struct ScanResult {
        QVariantList models;
        QStringList directories;
        QHash<QString, QVariantMap> cache;
        QString warning;
    };
    static ScanResult scan(const QString &, const QHash<QString, QVariantMap> &,
                           const std::shared_ptr<std::atomic_bool> &);
    void beginScan();
    QString m_directory;
    QVariantList m_models;
    QHash<QString, QVariantMap> m_cache;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QTimer m_periodicRefresh;
    QFutureWatcher<ScanResult> m_future;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QString m_warning;
    bool m_scanning = false;
    bool m_rescan = false;
};
} // namespace titan
