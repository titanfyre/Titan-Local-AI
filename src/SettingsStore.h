#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>
namespace titan {
class SettingsStore {
  public:
    SettingsStore();
    QString dataPath() const { return dataPath_; }
    QVariantMap settings() const { return settings_; }
    bool update(const QString &key, const QVariant &value);
    QVariant value(const QString &key) const { return settings_.value(key); }
    QString modelsDirectory() const;
    void setModelsDirectory(const QString &path);
    QJsonArray grants() const;
    void setGrants(const QJsonArray &value);
    QVariantList sessions() const;
    QJsonObject loadSession(const QString &id) const;
    bool saveSession(const QString &id, const QString &title, const QJsonArray &ui,
                     const QJsonArray &history);
    bool deleteSession(const QString &id);
    void save();

  private:
    QString dataPath_;
    QVariantMap settings_;
    QJsonObject config_;
    static bool validId(const QString &id);
};
} // namespace titan
