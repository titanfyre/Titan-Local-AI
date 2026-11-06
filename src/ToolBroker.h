#pragma once
#include "BrowserTools.h"
#include "tools/WorkspaceAccess.h"
#include <QFutureWatcher>
#include <QObject>
#include <memory>
namespace titan {
class ToolBroker final : public QObject {
    Q_OBJECT
  public:
    explicit ToolBroker(const QString &privateData, QObject *parent = nullptr);
    QJsonArray definitions() const;
    QString permissionSummary() const;
    void setGrants(QList<FolderGrant> grants);
    void protectModels(const QString &path);
    QList<FolderGrant> grants() const;
    bool mutating(const QString &name, const QJsonObject &args) const;
    void execute(const QString &id, const QString &name, const QJsonObject &args);
    bool browserConnected() const;
  signals:
    void completed(QString id, QJsonObject result);
    void browserStateChanged();

  private:
    std::shared_ptr<WorkspaceAccess> workspace_;
    std::shared_ptr<BrowserTools> browser_;
    QString privateData_;
};
} // namespace titan
