#include "ChatModel.h"
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>
namespace titan {
ChatModel::ChatModel(QObject *parent) : QAbstractListModel(parent) {
    flush_.setSingleShot(true);
    flush_.setInterval(24);
    connect(&flush_, &QTimer::timeout, this, [this] {
        const auto copy = dirty_;
        dirty_.clear();
        for (int i : copy)
            if (i >= 0 && i < rows_.size())
                emit dataChanged(index(i), index(i), {TextRole, ReasoningRole, StateRole});
    });
}
int ChatModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}
QVariant ChatModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size())
        return {};
    const auto &m = rows_[index.row()];
    switch (role) {
    case IdRole:
        return m.id;
    case RoleRole:
        return m.role;
    case TextRole:
        return safeMarkdown(m.text);
    case ReasoningRole:
        return m.reasoning;
    case StateRole:
        return m.state;
    case ToolRole:
        return m.tool;
    default:
        return {};
    }
}
QHash<int, QByteArray> ChatModel::roleNames() const {
    return {{IdRole, "messageId"},       {RoleRole, "messageRole"},
            {TextRole, "messageText"},   {ReasoningRole, "reasoningText"},
            {StateRole, "messageState"}, {ToolRole, "toolLabel"}};
}
QString ChatModel::add(QString role, QString text, QString state, QString tool) {
    const int i = static_cast<int>(rows_.size());
    beginInsertRows({}, i, i);
    rows_.append({QUuid::createUuid().toString(QUuid::WithoutBraces),
                  std::move(role),
                  std::move(text),
                  {},
                  std::move(state),
                  std::move(tool)});
    endInsertRows();
    return rows_.last().id;
}
int ChatModel::find(const QString &id) const {
    for (int i = 0; i < rows_.size(); ++i)
        if (rows_[i].id == id)
            return i;
    return -1;
}
void ChatModel::append(const QString &id, const QString &text, bool reasoning) {
    int i = find(id);
    if (i < 0)
        return;
    auto &m = rows_[i];
    if (reasoning)
        m.reasoning += text;
    else
        m.text += text;
    dirty_.insert(i);
    if (!flush_.isActive())
        flush_.start();
}
void ChatModel::finish(const QString &id, const QString &state) {
    int i = find(id);
    if (i < 0)
        return;
    rows_[i].state = state;
    emit dataChanged(index(i), index(i));
}
void ChatModel::setText(const QString &id, const QString &text) {
    int i = find(id);
    if (i < 0)
        return;
    rows_[i].text = text;
    emit dataChanged(index(i), index(i));
}
void ChatModel::clear() {
    flush_.stop();
    dirty_.clear();
    beginResetModel();
    rows_.clear();
    endResetModel();
}
QJsonArray ChatModel::toJson() const {
    QJsonArray out;
    for (const auto &m : rows_)
        out.append(QJsonObject{{"id", m.id},
                               {"role", m.role},
                               {"text", m.text},
                               {"reasoning", m.reasoning},
                               {"state", m.state},
                               {"tool", m.tool}});
    return out;
}
void ChatModel::fromJson(const QJsonArray &messages) {
    clear();
    beginResetModel();
    for (const auto &v : messages) {
        auto o = v.toObject();
        QString state = o.value("state").toString();
        if (state == "running" || state == "streaming")
            state = "done";
        rows_.append({o.value("id").toString(), o.value("role").toString(), o.value("text").toString(),
                      o.value("reasoning").toString(), state, o.value("tool").toString()});
    }
    endResetModel();
}
QString ChatModel::safeMarkdown(const QString &text) {
    QStringList lines = text.split('\n');
    bool fenced = false;
    static const QRegularExpression image(R"(!\[[^\]\n]*\]\([^\n]*?\))");
    static const QRegularExpression htmlImage(R"(<\s*(?:img|iframe|object|video|audio|source)\b[^>]*>)",
                                              QRegularExpression::CaseInsensitiveOption);
    for (QString &line : lines) {
        if (line.trimmed().startsWith("```")) {
            fenced = !fenced;
            continue;
        }
        if (!fenced) {
            line.replace(image, "[Image non chargée]");
            line.remove(htmlImage);
        }
    }
    return lines.join('\n');
}
} // namespace titan
