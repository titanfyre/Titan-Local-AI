#pragma once
#include <BrowserControl.h>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <optional>
namespace titan {
class BrowserTools final {
  public:
    explicit BrowserTools(QString privateData);
    ~BrowserTools();
    QJsonObject invoke(const QString &name, const QJsonObject &args);
    static QJsonArray toolDefinitions();
    bool connected() const;
    void shutdown();

  private:
    QString data_;
    mutable QMutex mutex_;
    std::optional<BrowserControlSDK::Browser> browser_;
    QString activeTab_;
    void ensure();
    BrowserControlSDK::Tab tab(const QJsonObject &args);
    QJsonObject observe(BrowserControlSDK::Tab &tab, int elements, int text);
    static bool allowedUrl(const QString &url);
    static QJsonObject toQt(const BrowserControlSDK::Json &value);
    static BrowserControlSDK::Json fromQt(const QJsonObject &value);
};
} // namespace titan
