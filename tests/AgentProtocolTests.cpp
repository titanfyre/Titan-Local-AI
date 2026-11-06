#include "LocalApiClient.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>

#include <functional>
#include <utility>

using titan::LocalApiClient;

namespace {

// A real loopback HTTP peer. Delayed byte-sized writes exercise the network
// client's buffer rather than calling its private parser directly.
class FakeLocalServer final : public QObject {
public:
    FakeLocalServer()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                auto *socket = m_server.nextPendingConnection();
                socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    m_request += socket->readAll();
                    if (m_handled) return;
                    const qsizetype separator = m_request.indexOf("\r\n\r\n");
                    if (separator < 0) return;
                    m_headers = m_request.left(separator);
                    qint64 contentLength = 0;
                    for (const auto &line : m_headers.split('\n')) {
                        if (line.toLower().startsWith("content-length:"))
                            contentLength = line.mid(15).trimmed().toLongLong();
                    }
                    if (m_request.size() - separator - 4 < contentLength) return;
                    m_body = m_request.mid(separator + 4, contentLength);
                    m_handled = true;
                    ++requests;
                    if (respond) respond(socket);
                });
            }
        });
        m_server.listen(QHostAddress::LocalHost, 0);
    }

    bool listening() const { return m_server.isListening(); }
    QUrl url(const QString &path = {}) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path));
    }
    QByteArray requestHeaders() const { return m_headers; }
    QJsonObject requestBody() const { return QJsonDocument::fromJson(m_body).object(); }

    void send(QTcpSocket *socket, const QList<QByteArray> &chunks,
              const QByteArray &contentType = "text/event-stream", int status = 200,
              int intervalMs = 2, const QByteArray &extraHeaders = {})
    {
        qint64 length = 0;
        for (const auto &chunk : chunks) length += chunk.size();
        socket->write("HTTP/1.1 " + QByteArray::number(status) + " Test\r\nContent-Type: " + contentType
                      + "\r\nContent-Length: " + QByteArray::number(length)
                      + "\r\nConnection: close\r\n" + extraHeaders + "\r\n");
        const QPointer<QTcpSocket> peer(socket);
        if (chunks.isEmpty()) { socket->disconnectFromHost(); return; }
        auto *timer = new QTimer(socket);
        timer->setTimerType(Qt::PreciseTimer);
        connect(timer, &QTimer::timeout, this, [this, peer, timer, chunks, index = qsizetype{0}]() mutable {
            if (!peer || peer->state() != QAbstractSocket::ConnectedState) { timer->stop(); return; }
            peer->write(chunks[index++]);
            ++chunksSent;
            if (index == chunks.size()) {
                timer->stop();
                timer->deleteLater();
                peer->disconnectFromHost();
            }
        });
        timer->start(intervalMs);
    }

    std::function<void(QTcpSocket *)> respond;
    int requests = 0;
    int chunksSent = 0;

private:
    QTcpServer m_server;
    QByteArray m_request, m_headers, m_body;
    bool m_handled = false;
};

QByteArray sseEvent(const QJsonObject &delta, bool trailingNewline = true)
{
    const QJsonObject object{{"choices", QJsonArray{QJsonObject{{"delta", delta}}}}};
    return "data: " + QJsonDocument(object).toJson(QJsonDocument::Compact) + (trailingNewline ? "\r\n\r\n" : "");
}

QList<QByteArray> fragmented(const QByteArray &bytes)
{
    // Single-byte pieces guarantee that multibyte UTF-8 characters, JSON tokens,
    // CRLF separators and SSE field names are divided across writes.
    QList<QByteArray> result;
    result.reserve(bytes.size());
    for (const char byte : bytes) result.append(QByteArray(1, byte));
    return result;
}

QString combined(const QSignalSpy &spy)
{
    QString text;
    for (const auto &arguments : spy) text += arguments.first().toString();
    return text;
}

QJsonObject payload()
{
    return {{"model", "test-local-model"}, {"messages", QJsonArray{QJsonObject{{"role", "user"}, {"content", "test"}}}}};
}

QJsonObject result(const QSignalSpy &spy)
{
    return spy.first().first().value<QJsonObject>();
}

} // namespace

class AgentProtocolTests final : public QObject {
    Q_OBJECT
private slots:
    void arbitrarilySplitSseAccumulatesUtf8TextReasoningAndUsage()
    {
        FakeLocalServer server;
        QVERIFY(server.listening());
        const QString accent = QString::fromUtf8("\xC3\xA9");
        const QString cup = QString::fromUtf8("\xE2\x98\x95");
        const QByteArray bytes = ": keepalive\r\n\r\n"
            + sseEvent({{"content", "Caf"}, {"reasoning_content", "Let us "}})
            + sseEvent({{"content", accent + u' '}, {"reasoning", "think."}})
            + sseEvent({{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", cup}}, QJsonObject{{"type", "text"}, {"text", "\nDone"}}}}})
            + "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":42,\"completion_tokens\":7,\"total_tokens\":49}}\r\n\r\n"
            + "data: [DONE]\r\n\r\n";
        server.respond = [&](QTcpSocket *socket) { server.send(socket, fragmented(bytes), "text/event-stream", 200, 1); };
        LocalApiClient client;
        QSignalSpy text(&client, &LocalApiClient::textDelta), reasoning(&client, &LocalApiClient::reasoningDelta);
        QSignalSpy completed(&client, &LocalApiClient::completed), failed(&client, &LocalApiClient::failed);
        client.request(server.url(), payload(), QStringLiteral("fake-local-test-key"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.count() + failed.count(), 1, 6000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(completed.count(), 1);
        QCOMPARE(combined(text), QStringLiteral("Caf") + accent + u' ' + cup + QStringLiteral("\nDone"));
        QCOMPARE(combined(reasoning), QStringLiteral("Let us think."));
        const auto completion = result(completed);
        QCOMPARE(completion.value("role").toString(), QStringLiteral("assistant"));
        QCOMPARE(completion.value("content").toString(), combined(text));
        QCOMPARE(completion.value("reasoning_content").toString(), combined(reasoning));
        QCOMPARE(completion.value("usage").toObject().value("total_tokens").toInt(), 49);
        QVERIFY(completion.value("tool_calls").toArray().isEmpty());
        QVERIFY(server.chunksSent > 100);
        QCOMPARE(server.requests, 1);
        QVERIFY(server.requestHeaders().startsWith("POST /v1/chat/completions HTTP/1.1\r\n"));
        QVERIFY(server.requestHeaders().toLower().contains("authorization: bearer fake-local-test-key"));
        QVERIFY(server.requestBody().value("stream").toBool());
        QCOMPARE(server.requestBody().value("model").toString(), QStringLiteral("test-local-model"));
    }

    void nativeToolCallFragmentsAccumulateAndKeepIndexOrder()
    {
        FakeLocalServer server;
        QVERIFY(server.listening());
        const auto toolPart = [](int index, const QString &id, const QString &name, const QString &arguments) {
            return QJsonObject{{"index", index}, {"id", id}, {"function", QJsonObject{{"name", name}, {"arguments", arguments}}}};
        };
        const QByteArray bytes = sseEvent({{"tool_calls", QJsonArray{toolPart(1, "second_", "files_", "{\"query\":")}}})
            + sseEvent({{"tool_calls", QJsonArray{toolPart(0, "first_", "files_", "{\"pa")}}})
            + sseEvent({{"tool_calls", QJsonArray{toolPart(1, "id", "search", "\"needle\"}"), toolPart(0, "id", "read", "th\":\"C:/work/a.txt\"}")}}})
            + "data: [DONE]\n\n";
        server.respond = [&](QTcpSocket *socket) { server.send(socket, fragmented(bytes), "text/event-stream", 200, 1); };
        LocalApiClient client;
        QSignalSpy completed(&client, &LocalApiClient::completed), failed(&client, &LocalApiClient::failed);
        client.request(server.url(QStringLiteral("/v1/")), payload());
        QTRY_COMPARE_WITH_TIMEOUT(completed.count() + failed.count(), 1, 6000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(completed.count(), 1);
        const auto calls = result(completed).value("tool_calls").toArray();
        QCOMPARE(calls.size(), 2);
        const auto first = calls[0].toObject(), second = calls[1].toObject();
        QCOMPARE(first.value("id").toString(), QStringLiteral("first_id"));
        QCOMPARE(second.value("id").toString(), QStringLiteral("second_id"));
        QCOMPARE(first.value("type").toString(), QStringLiteral("function"));
        QCOMPARE(first.value("function").toObject().value("name").toString(), QStringLiteral("files_read"));
        QCOMPARE(second.value("function").toObject().value("name").toString(), QStringLiteral("files_search"));
        QCOMPARE(first.value("function").toObject().value("arguments").toString(), QStringLiteral("{\"path\":\"C:/work/a.txt\"}"));
        QCOMPARE(second.value("function").toObject().value("arguments").toString(), QStringLiteral("{\"query\":\"needle\"}"));
    }

    void nonSseCompletionFallback()
    {
        FakeLocalServer server;
        QVERIFY(server.listening());
        const QString text = QString::fromUtf8("Fallback caf\xC3\xA9");
        const QJsonObject call{{"id", "fallback_call"}, {"type", "function"},
            {"function", QJsonObject{{"name", "files_list"}, {"arguments", "{\"path\":\"C:/work\"}"}}}};
        const QJsonObject response{{"choices", QJsonArray{QJsonObject{{"message", QJsonObject{{"content", text},
            {"reasoning_content", "Fallback reasoning"}, {"tool_calls", QJsonArray{call}}}}}}},
            {"usage", QJsonObject{{"prompt_tokens", 3}, {"completion_tokens", 4}, {"total_tokens", 7}}}};
        const QByteArray bytes = QJsonDocument(response).toJson(QJsonDocument::Compact);
        server.respond = [&](QTcpSocket *socket) { server.send(socket, fragmented(bytes), "application/json", 200, 1); };
        LocalApiClient client;
        QSignalSpy completed(&client, &LocalApiClient::completed), failed(&client, &LocalApiClient::failed);
        QSignalSpy deltas(&client, &LocalApiClient::textDelta), reasoning(&client, &LocalApiClient::reasoningDelta);
        client.request(server.url(), payload());
        QTRY_COMPARE_WITH_TIMEOUT(completed.count() + failed.count(), 1, 6000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(completed.count(), 1);
        QCOMPARE(result(completed).value("content").toString(), text);
        QCOMPARE(combined(deltas), text);
        QCOMPARE(combined(reasoning), QStringLiteral("Fallback reasoning"));
        QCOMPARE(result(completed).value("usage").toObject().value("total_tokens").toInt(), 7);
        QCOMPARE(result(completed).value("tool_calls").toArray().first().toObject().value("id").toString(), QStringLiteral("fallback_call"));
    }

    void finalSseEventWithoutTrailingNewline()
    {
        FakeLocalServer server;
        QVERIFY(server.listening());
        const QByteArray bytes = sseEvent({{"content", "start "}}) + sseEvent({{"content", "end"}}, false);
        server.respond = [&](QTcpSocket *socket) { server.send(socket, fragmented(bytes), "text/event-stream", 200, 1); };
        LocalApiClient client;
        QSignalSpy completed(&client, &LocalApiClient::completed), failed(&client, &LocalApiClient::failed);
        client.request(server.url(), payload());
        QTRY_COMPARE_WITH_TIMEOUT(completed.count() + failed.count(), 1, 4000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(completed.count(), 1);
        QCOMPARE(result(completed).value("content").toString(), QStringLiteral("start end"));
    }

    void singleSseEventWithoutTrailingNewline()
    {
        FakeLocalServer server;
        QVERIFY(server.listening());
        const QByteArray bytes = sseEvent({{"content", "complete event"}}, false);
        server.respond = [&](QTcpSocket *socket) { server.send(socket, fragmented(bytes), "text/event-stream", 200, 1); };
        LocalApiClient client;
        QSignalSpy completed(&client, &LocalApiClient::completed), failed(&client, &LocalApiClient::failed);
        client.request(server.url(), payload());
        QTRY_COMPARE_WITH_TIMEOUT(completed.count() + failed.count(), 1, 4000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(completed.count(), 1);
        QCOMPARE(result(completed).value("content").toString(), QStringLiteral("complete event"));
    }

    void httpErrorHasOneFailureAndNoCompletion()
    {
        FakeLocalServer server;
        QVERIFY(server.listening());
        server.respond = [&](QTcpSocket *socket) {
            server.send(socket, {QByteArray("{\"error\":{\"message\":\"model unavailable\"}}")}, "application/json", 503);
        };
        LocalApiClient client;
        QSignalSpy failed(&client, &LocalApiClient::failed), completed(&client, &LocalApiClient::completed);
        QSignalSpy text(&client, &LocalApiClient::textDelta);
        client.request(server.url(), payload());
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
        QCOMPARE(completed.count(), 0);
        QCOMPARE(text.count(), 0);
        QVERIFY(failed.first().first().toString().contains("503"));
        QVERIFY(failed.first().first().toString().contains("model unavailable"));
    }

    void redirectsAreNotFollowed()
    {
        FakeLocalServer original, destination;
        QVERIFY(original.listening());
        QVERIFY(destination.listening());
        original.respond = [&](QTcpSocket *socket) {
            original.send(socket, {}, "text/plain", 302, 1, "Location: " + destination.url().toEncoded() + "\r\n");
        };
        LocalApiClient client;
        QSignalSpy failed(&client, &LocalApiClient::failed), completed(&client, &LocalApiClient::completed);
        client.request(original.url(), payload());
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
        QCOMPARE(completed.count(), 0);
        QCOMPARE(destination.requests, 0);
        QVERIFY(failed.first().first().toString().contains("302"));
    }

    void cancellationStopsAllFurtherCallbacks()
    {
        FakeLocalServer server;
        QVERIFY(server.listening());
        server.respond = [&](QTcpSocket *socket) {
            server.send(socket, {sseEvent({{"content", "first"}}), sseEvent({{"content", "late"}}), QByteArray("data: [DONE]\n\n")}, "text/event-stream", 200, 100);
        };
        LocalApiClient client;
        QSignalSpy text(&client, &LocalApiClient::textDelta), reasoning(&client, &LocalApiClient::reasoningDelta);
        QSignalSpy failed(&client, &LocalApiClient::failed), completed(&client, &LocalApiClient::completed), cancelled(&client, &LocalApiClient::cancelled);
        client.request(server.url(), payload());
        QTRY_COMPARE_WITH_TIMEOUT(text.count(), 1, 3000);
        client.cancel();
        QCOMPARE(cancelled.count(), 1);
        QTimer waitUntilPeerFinished;
        waitUntilPeerFinished.setSingleShot(true);
        QSignalSpy elapsed(&waitUntilPeerFinished, &QTimer::timeout);
        waitUntilPeerFinished.start(350);
        QTRY_COMPARE_WITH_TIMEOUT(elapsed.count(), 1, 1500);
        QCOMPARE(text.count(), 1);
        QCOMPARE(combined(text), QStringLiteral("first"));
        QCOMPARE(reasoning.count(), 0);
        QCOMPARE(completed.count(), 0);
        QCOMPARE(failed.count(), 0);
        client.cancel();
        QCOMPARE(cancelled.count(), 1);
    }

    void invalidEndpointsAreRejected_data()
    {
        QTest::addColumn<QUrl>("url");
        QTest::newRow("remote-domain") << QUrl("https://example.com/v1");
        QTest::newRow("lan-address") << QUrl("http://192.168.1.15:8080");
        QTest::newRow("localhost-suffix") << QUrl("http://localhost.example.com");
        QTest::newRow("userinfo-host-trick") << QUrl("http://127.0.0.1@example.com");
        QTest::newRow("credentials") << QUrl("http://user:password@127.0.0.1:8080");
        QTest::newRow("username") << QUrl("http://user@localhost:8080");
        QTest::newRow("fragment") << QUrl("http://127.0.0.1:8080/#fragment");
        QTest::newRow("unsupported-scheme") << QUrl("ftp://127.0.0.1");
        QTest::newRow("file-url") << QUrl("file:///C:/work/file.txt");
        QTest::newRow("empty-url") << QUrl();
    }

    void invalidEndpointsAreRejected()
    {
        QFETCH(QUrl, url);
        QVERIFY(!LocalApiClient::isLocalEndpoint(url));
        LocalApiClient client;
        QSignalSpy failed(&client, &LocalApiClient::failed), completed(&client, &LocalApiClient::completed), text(&client, &LocalApiClient::textDelta);
        client.request(url, payload());
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
        QCOMPARE(completed.count(), 0);
        QCOMPARE(text.count(), 0);
        QVERIFY(!failed.first().first().toString().isEmpty());
    }

    void loopbackEndpointsAreAccepted_data()
    {
        QTest::addColumn<QUrl>("url");
        QTest::newRow("localhost") << QUrl("http://localhost:8080");
        QTest::newRow("uppercase-localhost") << QUrl("http://LOCALHOST:8080/v1");
        QTest::newRow("ipv4") << QUrl("http://127.0.0.1:8080");
        QTest::newRow("ipv4-loopback-range") << QUrl("http://127.0.0.2:8080");
        QTest::newRow("ipv6") << QUrl("http://[::1]:8080");
        QTest::newRow("https-localhost") << QUrl("https://localhost:8443");
    }

    void loopbackEndpointsAreAccepted()
    {
        QFETCH(QUrl, url);
        QVERIFY(LocalApiClient::isLocalEndpoint(url));
    }

    void completionUrlNormalization_data()
    {
        QTest::addColumn<QUrl>("base");
        QTest::addColumn<QUrl>("expected");
        QTest::newRow("origin") << QUrl("http://localhost:8080") << QUrl("http://localhost:8080/v1/chat/completions");
        QTest::newRow("root-slashes") << QUrl("http://localhost:8080///") << QUrl("http://localhost:8080/v1/chat/completions");
        QTest::newRow("v1") << QUrl("http://localhost:8080/v1/") << QUrl("http://localhost:8080/v1/chat/completions");
        QTest::newRow("prefix") << QUrl("http://localhost:8080/api") << QUrl("http://localhost:8080/api/v1/chat/completions");
        QTest::newRow("prefix-v1") << QUrl("http://localhost:8080/api/v1///") << QUrl("http://localhost:8080/api/v1/chat/completions");
        QTest::newRow("complete") << QUrl("http://localhost:8080/v1/chat/completions") << QUrl("http://localhost:8080/v1/chat/completions");
        QTest::newRow("complete-with-query") << QUrl("http://localhost:8080/v1/chat/completions/?old=value") << QUrl("http://localhost:8080/v1/chat/completions");
        QTest::newRow("ipv6-and-https") << QUrl("https://[::1]:8443/custom/v1") << QUrl("https://[::1]:8443/custom/v1/chat/completions");
    }

    void completionUrlNormalization()
    {
        QFETCH(QUrl, base);
        QFETCH(QUrl, expected);
        QCOMPARE(LocalApiClient::completionUrl(base), expected);
    }
};

QTEST_GUILESS_MAIN(AgentProtocolTests)
#include "AgentProtocolTests.moc"
