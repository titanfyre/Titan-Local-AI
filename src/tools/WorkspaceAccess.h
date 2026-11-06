#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QStringList>

namespace titan {

struct FolderGrant {
    QString path;
    bool writable = false;
};

// The first protected root is the application's private data directory. Backups
// are stored below it. An empty list uses QStandardPaths::AppLocalDataLocation.
// All methods may be called from different threads; operations are serialized
// with grant changes. Destroy this object only after its callers have stopped.
class WorkspaceAccess final {
public:
    explicit WorkspaceAccess(QStringList protectedRoots);

    void setGrants(QList<FolderGrant> grants);
    [[nodiscard]] QList<FolderGrant> grants() const;
    [[nodiscard]] QJsonObject invoke(const QString &tool, const QJsonObject &arguments);
    [[nodiscard]] static QJsonArray toolDefinitions();

    static constexpr qint64 MaxTextBytes = 1024 * 1024;
    static constexpr int MaxTraversalEntries = 5000;
    static constexpr int MaxSearchFiles = 1000;
    static constexpr qint64 MaxSearchBytes = 16 * 1024 * 1024;
    static constexpr int MaxDepth = 32;

private:
    struct AuthorizedPath {
        QString path;
        bool exists = false;
        bool directory = false;
    };

    [[nodiscard]] bool authorize(const QString &input, bool write, bool allowMissing,
                                 AuthorizedPath &result, QString &code) const;
    [[nodiscard]] bool protectedPath(const QString &path) const;
    [[nodiscard]] QJsonObject list(const QJsonObject &arguments);
    [[nodiscard]] QJsonObject read(const QJsonObject &arguments);
    [[nodiscard]] QJsonObject search(const QJsonObject &arguments);
    [[nodiscard]] QJsonObject write(const QJsonObject &arguments, bool replace);
    [[nodiscard]] bool backup(const QByteArray &bytes);

    mutable QMutex m_mutex;
    QList<FolderGrant> m_grants;
    QStringList m_protectedRoots;
    QString m_backupRoot;
    QHash<QString, QString> m_readHashes;
};

} // namespace titan
