#include "ToolBroker.h"
#include <QJsonDocument>
#include <QtConcurrent>
namespace titan {
ToolBroker::ToolBroker(const QString &p, QObject *parent)
    : QObject(parent), workspace_(std::make_shared<WorkspaceAccess>(QStringList{p})),
      browser_(std::make_shared<BrowserTools>(p)), privateData_(p) {}
void ToolBroker::protectModels(const QString &path) {
    auto grants = workspace_->grants();
    workspace_ = std::make_shared<WorkspaceAccess>(QStringList{privateData_, path});
    workspace_->setGrants(grants);
}
QJsonArray ToolBroker::definitions() const {
    auto a = WorkspaceAccess::toolDefinitions();
    for (const auto &v : BrowserTools::toolDefinitions())
        a.append(v);
    return a;
}
QString ToolBroker::permissionSummary() const {
    QJsonArray a;
    for (const auto &g : workspace_->grants())
        a.append(QJsonObject{{"path", g.path}, {"writable", g.writable}});
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}
void ToolBroker::setGrants(QList<FolderGrant> g) {
    workspace_->setGrants(std::move(g));
}
QList<FolderGrant> ToolBroker::grants() const {
    return workspace_->grants();
}
bool ToolBroker::browserConnected() const {
    return browser_->connected();
}
bool ToolBroker::mutating(const QString &name, const QJsonObject &a) const {
    return name == "files_write" || name == "files_replace" || name == "browser_act" ||
           name == "browser_navigate" ||
           (name == "browser_tabs" && a.value("operation").toString("list") != "list");
}
void ToolBroker::execute(const QString &id, const QString &name, const QJsonObject &args) {
    auto *watcher = new QFutureWatcher<QJsonObject>(this);
    connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, id] {
        auto result = watcher->result();
        watcher->deleteLater();
        emit browserStateChanged();
        emit completed(id, result);
    });
    auto fs = workspace_;
    auto browser = browser_;
    watcher->setFuture(QtConcurrent::run([fs, browser, name, args] {
        if (name.startsWith("files_"))
            return fs->invoke(name, args);
        if (name.startsWith("browser_"))
            return browser->invoke(name, args);
        return QJsonObject{{"ok", false},
                           {"error", QJsonObject{{"code", "unknown_tool"}, {"message", "Outil inconnu."}}}};
    }));
}
} // namespace titan
