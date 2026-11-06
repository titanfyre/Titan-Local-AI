#pragma once
#include <QAbstractListModel>
#include <QJsonArray>
#include <QSet>
#include <QTimer>

namespace titan {
class ChatModel final : public QAbstractListModel {
    Q_OBJECT
  public:
    enum Roles { IdRole = Qt::UserRole + 1, RoleRole, TextRole, ReasoningRole, StateRole, ToolRole };
    struct Message {
        QString id, role, text, reasoning, state, tool;
    };
    explicit ChatModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString add(QString role, QString text = {}, QString state = "done", QString tool = {});
    void append(const QString &id, const QString &text, bool reasoning = false);
    void finish(const QString &id, const QString &state = "done");
    void setText(const QString &id, const QString &text);
    void clear();
    QJsonArray toJson() const;
    void fromJson(const QJsonArray &messages);
    static QString safeMarkdown(const QString &text);

  private:
    QList<Message> rows_;
    QTimer flush_;
    QSet<int> dirty_;
    int find(const QString &id) const;
};
} // namespace titan
