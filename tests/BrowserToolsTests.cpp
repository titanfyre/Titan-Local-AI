#include "BrowserTools.h"
#include <QFile>
#include <QFutureWatcher>
#include <QHostAddress>
#include <QJsonArray>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtConcurrent>
#include <QtTest>
using namespace titan;
class BrowserToolsTests : public QObject {
    Q_OBJECT
  private slots:
    void ownedBrowserRoundTrip() {
        if (!qEnvironmentVariableIsSet("TITAN_TEST_BROWSER"))
            QSKIP("Set TITAN_TEST_BROWSER=1 for the owned local WebView2 integration test.");
        QTemporaryDir data;
        QVERIFY(data.isValid());
        QTcpServer fixture;
        QVERIFY(fixture.listen(QHostAddress::LocalHost, 0));
        const QByteArray page =
            "<!doctype html><meta charset=utf-8><title>Titan owned fixture</title><h1>Titan browser "
            "test</h1><label>Name <input id=name></label><button id=go "
            "onclick=\"document.querySelector('#result').textContent='Saved "
            "'+document.querySelector('#name').value\">Save fixture</button><p id=result>Waiting</p>";
        connect(&fixture, &QTcpServer::newConnection, this, [&] {
            while (fixture.hasPendingConnections()) {
                auto *socket = fixture.nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [socket, page] {
                    auto request = socket->readAll();
                    if (!request.contains("\r\n\r\n"))
                        return;
                    socket->write(
                        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                        QByteArray::number(page.size()) + "\r\nConnection: close\r\n\r\n" + page);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        BrowserTools browser(data.path());
        auto call = [&](QString name, QJsonObject args = {}) {
            QFutureWatcher<QJsonObject> watcher;
            auto future = QtConcurrent::run([&browser, name, args] { return browser.invoke(name, args); });
            watcher.setFuture(future);
            QEventLoop wait;
            connect(&watcher, &QFutureWatcher<QJsonObject>::finished, &wait, &QEventLoop::quit);
            if (!watcher.isFinished())
                wait.exec();
            return watcher.result();
        };
        auto tabs = call("browser_tabs", {{"operation", "list"}});
        QVERIFY2(tabs.value("ok").toBool(), qPrintable(QString::fromUtf8(QJsonDocument(tabs).toJson())));
        QVERIFY(!tabs.value("tabs").toArray().isEmpty());
        const auto url = QString("http://127.0.0.1:%1/").arg(fixture.serverPort());
        auto observed = call("browser_navigate", {{"url", url}});
        QVERIFY2(observed.value("ok").toBool(),
                 qPrintable(QString::fromUtf8(QJsonDocument(observed).toJson())));
        QVERIFY(observed.value("text").toString().contains("Titan browser test"));
        auto result =
            call("browser_act",
                 {{"expected_url", url},
                  {"expected_generation", observed.value("generation")},
                  {"actions",
                   QJsonArray{QJsonObject{{"action", "fill"}, {"selector", "#name"}, {"text", "Local AI"}},
                              QJsonObject{{"action", "click"}, {"selector", "#go"}}}}});
        QVERIFY2(result.value("ok").toBool(), qPrintable(QString::fromUtf8(QJsonDocument(result).toJson())));
        QCOMPARE(result.value("status").toString(), QString("completed"));
        QVERIFY(result.value("observation").toObject().value("text").toString().contains("Saved Local AI"));
        auto shot = call("browser_screenshot");
        QVERIFY(shot.value("ok").toBool());
        QFile image(shot.value("path").toString());
        QVERIFY(image.open(QIODevice::ReadOnly));
        QCOMPARE(image.read(8), QByteArray::fromHex("89504e470d0a1a0a"));
        auto denied = call("browser_navigate", {{"url", "file:///C:/Windows/win.ini"}});
        QVERIFY(!denied.value("ok").toBool());
        auto guarded = [&](QJsonArray actions) {
            return QJsonObject{{"actions", actions},
                               {"expected_url", url},
                               {"expected_generation", observed.value("generation")}};
        };
        denied = call("browser_act",
                      {{"actions", QJsonArray{QJsonObject{{"action", "click"}, {"selector", "#go"}}}}});
        QCOMPARE(denied.value("error").toObject().value("code").toString(), QString("observation_required"));
        denied = call("browser_act", guarded(QJsonArray{QJsonObject{{"action", "key"}, {"key", "CTRL+O"}}}));
        QVERIFY(!denied.value("ok").toBool());
        QCOMPARE(denied.value("error").toObject().value("code").toString(), QString("key_denied"));
        denied =
            call("browser_act",
                 guarded(QJsonArray{QJsonObject{{"action", "evaluate"}, {"expression", "document.cookie"}}}));
        QVERIFY(!denied.value("ok").toBool());
        denied = call("browser_act",
                      guarded(QJsonArray{QJsonObject{
                          {"action", "click"}, {"selector", "#go"}, {"expression", "document.cookie"}}}));
        QCOMPARE(denied.value("error").toObject().value("code").toString(),
                 QString("invalid_action_arguments"));
        auto partial =
            call("browser_act",
                 guarded(QJsonArray{
                     QJsonObject{{"action", "fill"}, {"selector", "#name"}, {"text", "Partial fixture"}},
                     QJsonObject{{"action", "click"}, {"selector", "#missing-target"}}}));
        QVERIFY(!partial.value("ok").toBool());
        QCOMPARE(partial.value("status").toString(), QString("failed"));
        QCOMPARE(partial.value("completed").toInt(), 1);
        QCOMPARE(partial.value("results").toArray().size(), 2);
        auto closed = call("browser_tabs", {{"operation", "close"}});
        QVERIFY(closed.value("ok").toBool());
        auto remaining = call("browser_observe");
        QVERIFY(remaining.value("ok").toBool());
        browser.shutdown();
        QVERIFY(!browser.connected());
        QTest::qWait(500);
    }
};
QTEST_GUILESS_MAIN(BrowserToolsTests)
#include "BrowserToolsTests.moc"
