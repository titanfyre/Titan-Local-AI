#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QTimer>
namespace titan {
class LocalApiClient final : public QObject {
    Q_OBJECT
  public:
    explicit LocalApiClient(QObject *parent = nullptr);
    void request(const QUrl &baseUrl, QJsonObject payload, const QString &apiKey = {});
    void cancel();
    static bool isLocalEndpoint(const QUrl &url);
    static QUrl completionUrl(const QUrl &baseUrl);
  signals:
    void textDelta(QString text);
    void reasoningDelta(QString text);
    void completed(QJsonObject result);
    void failed(QString message);
    void cancelled();

  private:
    QNetworkAccessManager network_;
    QNetworkReply *reply_ = nullptr;
    QTimer timeout_;
    QByteArray buffer_, raw_;
    QString text_, reasoning_;
    QMap<int, QJsonObject> calls_;
    QJsonObject usage_;
    bool streaming_ = false;
    void consume();
    void event(const QByteArray &data);
    void acceptDelta(const QJsonObject &delta);
};
} // namespace titan
