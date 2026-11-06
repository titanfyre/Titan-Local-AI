#include "SettingsStore.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
namespace titan {
static QJsonObject readObject(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 32 * 1024 * 1024)
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
SettingsStore::SettingsStore() {
    dataPath_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataPath_ + "/chats");
    QDir().mkpath(dataPath_ + "/models");
    settings_ = {
        {"provider", "managed"},
        {"endpoint", "http://127.0.0.1:1234/v1"},
        {"externalModel", ""},
        {"temperature", 0.7},
        {"topP", 0.95},
        {"contextSize", 8192},
        {"maxTokens", 2048},
        {"threads", 0},
        {"gpuLayers", -1},
        {"thinkingLevel", "auto"},
        {"approvalsEnabled", true},
        {"toolMode", "auto"},
        {"maxSteps", 12},
        {"systemPrompt",
         "Tu es un assistant local utile, rigoureux et concis. Réponds dans la langue de l’utilisateur."},
        {"theme", "dark"},
        {"reducedMotion", false}};
    settings_["lastModelId"] = "";
    config_ = readObject(dataPath_ + "/settings.json");
    const auto saved = config_.value("settings").toObject();
    for (auto i = saved.begin(); i != saved.end(); ++i)
        update(i.key(), i.value().toVariant());
}
bool SettingsStore::update(const QString &key, const QVariant &value) {
    if (!settings_.contains(key))
        return false;
    if (key == "temperature")
        settings_[key] = qBound(0.0, value.toDouble(), 2.0);
    else if (key == "topP")
        settings_[key] = qBound(0.01, value.toDouble(), 1.0);
    else if (key == "contextSize")
        settings_[key] = qBound(512, value.toInt(), 131072);
    else if (key == "maxTokens")
        settings_[key] = qBound(64, value.toInt(), 32768);
    else if (key == "threads")
        settings_[key] = qBound(0, value.toInt(), 128);
    else if (key == "gpuLayers")
        settings_[key] = qBound(-1, value.toInt(), 999);
    else if (key == "maxSteps")
        settings_[key] = qBound(1, value.toInt(), 40);
    else if (key == "systemPrompt")
        settings_[key] = value.toString().left(16000);
    else if (key == "approvalsEnabled" || key == "reducedMotion")
        settings_[key] = value.toBool();
    else {
        const auto v = value.toString();
        if (key == "provider" && !QStringList{"managed", "ollama", "lmstudio", "custom"}.contains(v))
            return false;
        if (key == "theme" && !QStringList{"dark", "light", "system"}.contains(v))
            return false;
        if (key == "toolMode" && !QStringList{"auto", "native", "json", "off"}.contains(v))
            return false;
        if (key == "thinkingLevel" && !QStringList{"auto", "off", "on", "low", "medium", "high"}.contains(v))
            return false;
        settings_[key] = v.left(2048);
    }
    return true;
}
void SettingsStore::save() {
    config_["settings"] = QJsonObject::fromVariantMap(settings_);
    QSaveFile f(dataPath_ + "/settings.json");
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(config_).toJson());
        f.commit();
    }
}
QString SettingsStore::modelsDirectory() const {
    return config_.value("modelsDirectory").toString(dataPath_ + "/models");
}
void SettingsStore::setModelsDirectory(const QString &path) {
    config_["modelsDirectory"] = path;
    save();
}
QJsonArray SettingsStore::grants() const {
    return config_.value("grants").toArray();
}
void SettingsStore::setGrants(const QJsonArray &value) {
    config_["grants"] = value;
    save();
}
bool SettingsStore::validId(const QString &id) {
    return QRegularExpression("^[a-fA-F0-9-]{36}$").match(id).hasMatch();
}
QVariantList SettingsStore::sessions() const {
    QVariantList list;
    QDir d(dataPath_ + "/chats");
    for (const auto &name : d.entryList({"*.json"}, QDir::Files)) {
        auto o = readObject(d.filePath(name));
        if (!o.isEmpty())
            list.append(QVariantMap{{"id", o.value("id").toString()},
                                    {"title", o.value("title").toString()},
                                    {"updated", o.value("updated").toString()}});
    }
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("updated").toString() > b.toMap().value("updated").toString();
    });
    return list;
}
QJsonObject SettingsStore::loadSession(const QString &id) const {
    return validId(id) ? readObject(dataPath_ + "/chats/" + id + ".json") : QJsonObject{};
}
bool SettingsStore::saveSession(const QString &id, const QString &title, const QJsonArray &ui,
                                const QJsonArray &history) {
    if (!validId(id))
        return false;
    QJsonObject o{{"version", 1},
                  {"id", id},
                  {"title", title.left(100)},
                  {"updated", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                  {"messages", ui},
                  {"history", history}};
    QSaveFile f(dataPath_ + "/chats/" + id + ".json");
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return f.commit();
}
bool SettingsStore::deleteSession(const QString &id) {
    return validId(id) && QFile::remove(dataPath_ + "/chats/" + id + ".json");
}
} // namespace titan
