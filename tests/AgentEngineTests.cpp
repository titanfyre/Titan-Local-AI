#include "AgentEngine.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <functional>
#include <memory>

using namespace titan;

namespace {

class ModelServer final : public QObject {
public:
    struct Reply { QJsonObject body; int status = 200; };
    ModelServer()
    {
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                auto handled = std::make_shared<bool>(false);
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer, handled] {
                    *buffer += socket->readAll();
                    if (*handled) return;
                    const auto separator = buffer->indexOf("\r\n\r\n");
                    if (separator < 0) return;
                    qint64 length = 0;
                    for (const auto &line : buffer->left(separator).split('\n'))
                        if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toLongLong();
                    if (buffer->size() - separator - 4 < length) return;
                    *handled = true;
                    const auto request = QJsonDocument::fromJson(buffer->mid(separator + 4, length)).object();
                    requests.append(request);
                    const Reply response = reply ? reply(request, requests.size() - 1) : Reply{};
                    const auto bytes = QJsonDocument(response.body).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 " + QByteArray::number(response.status)
                                  + " Test\r\nContent-Type: application/json\r\nContent-Length: "
                                  + QByteArray::number(bytes.size()) + "\r\nConnection: close\r\n\r\n" + bytes);
                    socket->disconnectFromHost();
                });
            }
        });
        server.listen(QHostAddress::LocalHost, 0);
    }
    bool listening() const { return server.isListening(); }
    QUrl endpoint() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort())); }
    std::function<Reply(const QJsonObject &, int)> reply;
    QList<QJsonObject> requests;
private:
    QTcpServer server;
};

QJsonObject nativeCall(const QString &id, const QString &name, const QJsonObject &arguments)
{
    return {{"id", id}, {"type", "function"}, {"function", QJsonObject{{"name", name},
        {"arguments", QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact))}}}};
}

ModelServer::Reply completion(const QString &text = {}, const QJsonArray &calls = {})
{
    QJsonObject message{{"role", "assistant"}, {"content", text}};
    if (!calls.isEmpty()) message["tool_calls"] = calls;
    return {{{"choices", QJsonArray{QJsonObject{{"message", message}}}}}};
}

ModelServer::Reply jsonReply(const QJsonObject &object)
{
    return completion(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
}

bool put(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray get(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

QJsonObject decodedResult(const QJsonObject &message)
{
    return QJsonDocument::fromJson(message.value("content").toString().toUtf8()).object();
}

QHash<QString, QJsonObject> toolResults(const QJsonArray &history)
{
    QHash<QString, QJsonObject> result;
    for (const auto &value : history) {
        const auto message = value.toObject();
        if (message.value("role") == "tool") result.insert(message.value("tool_call_id").toString(), decodedResult(message));
    }
    return result;
}

QString code(const QJsonObject &result) { return result.value("error").toObject().value("code").toString(); }

QString historyProblem(const QJsonArray &history)
{
    QSet<QString> pending, allIds;
    for (const auto &value : history) {
        const auto message = value.toObject();
        const QString role = message.value("role").toString();
        if (role == "tool") {
            const auto id = message.value("tool_call_id").toString();
            if (!pending.remove(id)) return QStringLiteral("A tool result has no matching pending call: ") + id;
            if (decodedResult(message).isEmpty()) return QStringLiteral("A tool result is not a JSON object.");
            continue;
        }
        if (!pending.isEmpty()) return QStringLiteral("A non-tool message interrupts pending native calls.");
        for (const auto &callValue : message.value("tool_calls").toArray()) {
            const auto call = callValue.toObject();
            const auto id = call.value("id").toString();
            if (id.isEmpty() || allIds.contains(id)) return QStringLiteral("Missing or duplicate native tool-call ID.");
            allIds.insert(id);
            pending.insert(id);
        }
    }
    return pending.isEmpty() ? QString{} : QStringLiteral("Native calls remain without tool results.");
}

int approvals(const QSignalSpy &spy)
{
    int count = 0;
    for (const auto &arguments : spy) if (!arguments.first().toMap().isEmpty()) ++count;
    return count;
}

QVariantMap settings(const QString &mode = QStringLiteral("native"), bool approval = true, int steps = 12)
{
    return {{"toolMode", mode}, {"approvalsEnabled", approval}, {"maxSteps", steps},
        {"contextSize", 32768}, {"maxTokens", 512}, {"systemPrompt", "Test local tool behavior."}};
}

} // namespace

class AgentEngineTests final : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        temporary = std::make_unique<QTemporaryDir>();
        QVERIFY(temporary->isValid());
        workspace = temporary->path() + QStringLiteral("/workspace");
        privateData = temporary->path() + QStringLiteral("/private");
        QVERIFY(QDir().mkpath(workspace));
        QVERIFY(QDir().mkpath(privateData));
        sample = workspace + QStringLiteral("/sample.txt");
        QVERIFY(put(sample, "original text\n"));
        model = std::make_unique<ChatModel>();
        tools = std::make_unique<ToolBroker>(privateData);
        tools->setGrants({{workspace, true}});
        engine = std::make_unique<AgentEngine>(model.get(), tools.get());
    }

    void cleanup()
    {
        engine->cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!engine->busy(), 5000);
        engine.reset();
        tools.reset();
        model.reset();
        temporary.reset();
    }

    void nativeReadThenHashGuardedWriteRequiresApproval()
    {
        ModelServer server;
        QVERIFY(server.listening());
        server.reply = [&](const QJsonObject &request, int index) {
            if (index == 0) return completion({}, {nativeCall("read_1", "files_read", {{"path", sample}})});
            if (index == 1) {
                const auto messages = request.value("messages").toArray();
                const auto read = decodedResult(messages.last().toObject());
                return completion({}, {nativeCall("write_1", "files_write", {{"path", sample}, {"content", "updated text\n"}, {"expected_sha256", read.value("sha256")}})});
            }
            return completion("Update completed.");
        };
        QSignalSpy finished(engine.get(), &AgentEngine::finished), errors(engine.get(), &AgentEngine::error);
        QSignalSpy approval(engine.get(), &AgentEngine::approvalRequested), executed(tools.get(), &ToolBroker::completed);
        start(server);
        QTRY_COMPARE_WITH_TIMEOUT(approvals(approval), 1, 5000);
        QVERIFY(engine->busy());
        QCOMPARE(executed.count(), 1); // The read ran; the write is pending.
        QCOMPARE(get(sample), QByteArray("original text\n"));
        QCOMPARE(server.requests.size(), 2);
        engine->approve(true);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(executed.count(), 2);
        QCOMPARE(get(sample), QByteArray("updated text\n"));
        QCOMPARE(server.requests.size(), 3);
        QVERIFY2(historyProblem(engine->history()).isEmpty(), qPrintable(historyProblem(engine->history())));
        const auto results = toolResults(engine->history());
        QVERIFY(results.value("read_1").value("ok").toBool());
        QVERIFY(results.value("write_1").value("ok").toBool());
        QVERIFY(results.value("write_1").value("backup_created").toBool());
        QCOMPARE(engine->history().last().toObject().value("content").toString(), QStringLiteral("Update completed."));
        QCOMPARE(QDir(privateData + QStringLiteral("/workspace-backups")).entryList(QDir::Files).size(), 1);
    }

    void truncatedLargeReadPreservesHashForApprovedReplacement()
    {
        const QByteArray original = QByteArray("START replace_token END\n") + QByteArray(50000, 'x');
        QVERIFY(put(sample, original));
        const QString originalHash = QString::fromLatin1(QCryptographicHash::hash(original, QCryptographicHash::Sha256).toHex());
        ModelServer server;
        QVERIFY(server.listening());
        QJsonObject readSentToModel;
        server.reply = [&](const QJsonObject &request, int index) {
            if (index == 0) return completion({}, {nativeCall("large_read", "files_read", {{"path", sample}})});
            if (index == 1) {
                readSentToModel = decodedResult(request.value("messages").toArray().last().toObject());
                return completion({}, {nativeCall("large_replace", "files_replace", {{"path", sample},
                    {"old_text", "replace_token"}, {"new_text", "replacement_done"}, {"expected_sha256", readSentToModel.value("sha256")}})});
            }
            return completion("Large-file replacement completed.");
        };
        auto options = settings();
        options["contextSize"] = 8192;
        constexpr int contextCap = 8192 * 2 - 8000;
        QSignalSpy finished(engine.get(), &AgentEngine::finished), errors(engine.get(), &AgentEngine::error);
        QSignalSpy approval(engine.get(), &AgentEngine::approvalRequested), executed(tools.get(), &ToolBroker::completed);
        start(server, options);
        QTRY_COMPARE_WITH_TIMEOUT(approvals(approval), 1, 5000);
        QCOMPARE(executed.count(), 1);
        QCOMPARE(get(sample), original);
        const auto savedRead = toolResults(engine->history()).value("large_read");
        QVERIFY(savedRead.value("ok").toBool());
        QVERIFY(savedRead.value("truncated_for_context").toBool());
        QCOMPARE(savedRead.value("sha256").toString(), originalHash);
        QCOMPARE(savedRead.value("path").toString(), sample);
        QCOMPARE(savedRead.value("bytes").toInteger(), original.size());
        QVERIFY(savedRead.value("content").toString().size() < original.size());
        QCOMPARE(readSentToModel, savedRead);
        for (const auto &value : engine->history()) {
            const auto message = value.toObject();
            if (message.value("tool_call_id") == "large_read") QVERIFY(message.value("content").toString().size() <= contextCap);
        }
        engine->approve(true);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(executed.count(), 2);
        QByteArray expected = original;
        expected.replace("replace_token", "replacement_done");
        QCOMPARE(get(sample), expected);
        const auto replaced = toolResults(engine->history()).value("large_replace");
        QVERIFY(replaced.value("ok").toBool());
        QVERIFY(replaced.value("backup_created").toBool());
        const QDir backups(privateData + QStringLiteral("/workspace-backups"));
        const auto backupFiles = backups.entryList(QDir::Files);
        QCOMPARE(backupFiles.size(), 1);
        QCOMPARE(get(backups.filePath(backupFiles.first())), original);
        QVERIFY2(historyProblem(engine->history()).isEmpty(), qPrintable(historyProblem(engine->history())));
    }

    void autoFallbackUsesCompatibleJsonContext()
    {
        ModelServer server;
        QVERIFY(server.listening());
        server.reply = [&](const QJsonObject &, int index) {
            if (index == 0) return ModelServer::Reply{{{"error", QJsonObject{{"message", "tools are unsupported by this model template"}}}}, 400};
            if (index == 1) return jsonReply({{"tool", "files_read"}, {"arguments", QJsonObject{{"path", sample}}}});
            return jsonReply({{"final", "Read completed."}});
        };
        QSignalSpy finished(engine.get(), &AgentEngine::finished), errors(engine.get(), &AgentEngine::error);
        QSignalSpy executed(tools.get(), &ToolBroker::completed);
        start(server, settings("auto"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(executed.count(), 1);
        QCOMPARE(server.requests.size(), 3);
        QVERIFY(server.requests[0].contains("tools"));
        QVERIFY(!server.requests[1].contains("tools"));
        QVERIFY(!server.requests[2].contains("tools"));
        bool markedResult = false;
        for (const auto &value : server.requests[2].value("messages").toArray()) {
            const auto message = value.toObject();
            QVERIFY(message.value("role") != "tool");
            QVERIFY(!message.contains("tool_calls"));
            if (message.value("role") == "user" && message.value("content").toString().contains("<untrusted_tool_result")) {
                markedResult = true;
                QVERIFY(message.value("content").toString().contains("original text"));
                QVERIFY(message.value("content").toString().contains("</untrusted_tool_result>"));
            }
        }
        QVERIFY(markedResult);
        QVERIFY2(historyProblem(engine->history()).isEmpty(), qPrintable(historyProblem(engine->history())));
        QCOMPARE(engine->history().last().toObject().value("content").toString(), QStringLiteral("Read completed."));
    }

    void unknownAndMalformedToolsNeverReachBroker()
    {
        ModelServer server;
        QVERIFY(server.listening());
        auto malformed = nativeCall("malformed", "files_read", {});
        auto function = malformed.value("function").toObject();
        function["arguments"] = "{broken json";
        malformed["function"] = function;
        server.reply = [&](const QJsonObject &, int index) {
            return index == 0 ? completion({}, {nativeCall("shell", "shell_execute", {{"command", "anything"}}),
                nativeCall("delete", "files_delete", {{"path", sample}}), malformed}) : completion("No action executed.");
        };
        QSignalSpy finished(engine.get(), &AgentEngine::finished), executed(tools.get(), &ToolBroker::completed);
        QSignalSpy approval(engine.get(), &AgentEngine::approvalRequested);
        start(server);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(executed.count(), 0);
        QCOMPARE(approvals(approval), 0);
        QCOMPARE(get(sample), QByteArray("original text\n"));
        const auto results = toolResults(engine->history());
        QCOMPARE(results.size(), 3);
        for (const auto &value : results) QCOMPARE(code(value), QStringLiteral("invalid_tool_call"));
        QVERIFY2(historyProblem(engine->history()).isEmpty(), qPrintable(historyProblem(engine->history())));
        QVERIFY(!tools->browserConnected());
    }

    void duplicateNativeIdsAreNormalizedBeforeHistory()
    {
        ModelServer server;
        QVERIFY(server.listening());
        server.reply = [&](const QJsonObject &, int index) {
            return index == 0 ? completion({}, {nativeCall("duplicate", "files_read", {{"path", sample}}),
                nativeCall("duplicate", "files_list", {{"path", workspace}})}) : completion("Both reads completed.");
        };
        QSignalSpy finished(engine.get(), &AgentEngine::finished), executed(tools.get(), &ToolBroker::completed);
        start(server);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(executed.count(), 2);
        QCOMPARE(toolResults(engine->history()).size(), 2);
        QVERIFY2(historyProblem(engine->history()).isEmpty(), qPrintable(historyProblem(engine->history())));
    }

    void toolsOffIgnoresUnrequestedNativeCalls()
    {
        ModelServer server;
        QVERIFY(server.listening());
        const QString pending = workspace + QStringLiteral("/pending.txt");
        server.reply = [&](const QJsonObject &, int) {
            return completion("Tools are disabled.", {nativeCall("unrequested", "files_write", {{"path", pending}, {"content", "must not run"}})});
        };
        QSignalSpy finished(engine.get(), &AgentEngine::finished), executed(tools.get(), &ToolBroker::completed);
        start(server, settings("off", false));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(executed.count(), 0);
        QVERIFY(!QFileInfo::exists(pending));
        QVERIFY(!server.requests.first().contains("tools"));
        QVERIFY(!engine->history().last().toObject().contains("tool_calls"));
        QVERIFY2(historyProblem(engine->history()).isEmpty(), qPrintable(historyProblem(engine->history())));
    }

    void mutationClassificationIncludesBrowserNavigation()
    {
        QVERIFY(tools->mutating("files_write", {}));
        QVERIFY(tools->mutating("files_replace", {}));
        QVERIFY(tools->mutating("browser_navigate", {}));
        QVERIFY(tools->mutating("browser_act", {}));
        QVERIFY(tools->mutating("browser_tabs", {{"operation", "create"}}));
        QVERIFY(tools->mutating("browser_tabs", {{"operation", "activate"}}));
        QVERIFY(tools->mutating("browser_tabs", {{"operation", "close"}}));
        QVERIFY(!tools->mutating("browser_tabs", {{"operation", "list"}}));
        QVERIFY(!tools->mutating("files_read", {}));
        QVERIFY(!tools->browserConnected());
    }

    void decliningOneWriteSettlesWholeBatch()
    {
        ModelServer server;
        QVERIFY(server.listening());
        const QString first = workspace + QStringLiteral("/first.txt"), second = workspace + QStringLiteral("/second.txt");
        server.reply = [&](const QJsonObject &, int) {
            return completion({}, {nativeCall("first", "files_write", {{"path", first}, {"content", "first"}}),
                nativeCall("second", "files_write", {{"path", second}, {"content", "second"}})});
        };
        QSignalSpy finished(engine.get(), &AgentEngine::finished), approval(engine.get(), &AgentEngine::approvalRequested);
        QSignalSpy executed(tools.get(), &ToolBroker::completed);
        start(server);
        QTRY_COMPARE_WITH_TIMEOUT(approvals(approval), 1, 5000);
        engine->approve(false);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QTest::qWait(80); // Cross the queued nextTool boundary.
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(executed.count(), 0);
        QVERIFY(!QFileInfo::exists(first));
        QVERIFY(!QFileInfo::exists(second));
        const auto results = toolResults(engine->history());
        QCOMPARE(code(results.value("first")), QStringLiteral("user_declined"));
        QCOMPARE(code(results.value("second")), QStringLiteral("cancelled"));
        QVERIFY2(historyProblem(engine->history()).isEmpty(), qPrintable(historyProblem(engine->history())));
    }

    void cancellationDuringApprovalSettlesAllCalls()
    {
        ModelServer server;
        QVERIFY(server.listening());
        const QString first = workspace + QStringLiteral("/first.txt"), second = workspace + QStringLiteral("/second.txt");
        server.reply = [&](const QJsonObject &, int) {
            return completion({}, {nativeCall("first", "files_write", {{"path", first}, {"content", "first"}}),
                nativeCall("second", "files_write", {{"path", second}, {"content", "second"}})});
        };
        QSignalSpy finished(engine.get(), &AgentEngine::finished), approval(engine.get(), &AgentEngine::approvalRequested);
        QSignalSpy executed(tools.get(), &ToolBroker::completed);
        start(server);
        QTRY_COMPARE_WITH_TIMEOUT(approvals(approval), 1, 5000);
        engine->cancel();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        engine->approve(true); // A stale UI confirmation must have no effect.
        QTest::qWait(80);
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(executed.count(), 0);
        QVERIFY(!QFileInfo::exists(first));
        QVERIFY(!QFileInfo::exists(second));
        const auto results = toolResults(engine->history());
        QCOMPARE(code(results.value("first")), QStringLiteral("cancelled"));
        QCOMPARE(code(results.value("second")), QStringLiteral("cancelled"));
        QVERIFY2(historyProblem(engine->history()).isEmpty(), qPrintable(historyProblem(engine->history())));
    }

    void cancellationAfterReadPreventsQueuedWrites()
    {
        ModelServer server;
        QVERIFY(server.listening());
        const QString pending = workspace + QStringLiteral("/pending.txt");
        server.reply = [&](const QJsonObject &, int) {
            return completion({}, {nativeCall("read", "files_read", {{"path", sample}}),
                nativeCall("write", "files_write", {{"path", pending}, {"content", "must not run"}})});
        };
        QSignalSpy finished(engine.get(), &AgentEngine::finished), executed(tools.get(), &ToolBroker::completed);
        connect(tools.get(), &ToolBroker::completed, engine.get(), [&](const QString &, const QJsonObject &) { engine->cancel(); });
        start(server, settings("native", false));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QTest::qWait(80);
        QCOMPARE(executed.count(), 1);
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(!QFileInfo::exists(pending));
        const auto results = toolResults(engine->history());
        QVERIFY(results.value("read").value("ok").toBool());
        QCOMPARE(code(results.value("write")), QStringLiteral("cancelled"));
        QVERIFY2(historyProblem(engine->history()).isEmpty(), qPrintable(historyProblem(engine->history())));
    }

    void maxStepsStopsBeforeTheNextMutation()
    {
        ModelServer server;
        QVERIFY(server.listening());
        const QString pending = workspace + QStringLiteral("/pending.txt");
        server.reply = [&](const QJsonObject &, int) {
            return completion({}, {nativeCall("read", "files_read", {{"path", sample}}),
                nativeCall("write", "files_write", {{"path", pending}, {"content", "must not run"}}),
                nativeCall("after", "files_list", {{"path", workspace}})});
        };
        QSignalSpy finished(engine.get(), &AgentEngine::finished), errors(engine.get(), &AgentEngine::error);
        QSignalSpy executed(tools.get(), &ToolBroker::completed);
        start(server, settings("native", false, 1));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(errors.count(), 1);
        QCOMPARE(executed.count(), 1);
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(!QFileInfo::exists(pending));
        const auto results = toolResults(engine->history());
        QVERIFY(results.value("read").value("ok").toBool());
        QCOMPARE(code(results.value("write")), QStringLiteral("step_limit"));
        QCOMPARE(code(results.value("after")), QStringLiteral("cancelled"));
        QVERIFY2(historyProblem(engine->history()).isEmpty(), qPrintable(historyProblem(engine->history())));
    }

    void finalAnswerIsAllowedAfterLastPermittedTool()
    {
        ModelServer server;
        QVERIFY(server.listening());
        server.reply = [&](const QJsonObject &, int index) {
            return index == 0 ? completion({}, {nativeCall("read", "files_read", {{"path", sample}})}) : completion("Final answer.");
        };
        QSignalSpy finished(engine.get(), &AgentEngine::finished), errors(engine.get(), &AgentEngine::error);
        start(server, settings("native", false, 1));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(server.requests.size(), 2);
        QCOMPARE(engine->history().last().toObject().value("content").toString(), QStringLiteral("Final answer."));
        QVERIFY2(historyProblem(engine->history()).isEmpty(), qPrintable(historyProblem(engine->history())));
    }

    void invalidToolCallsStillConsumeStepBudget()
    {
        ModelServer server;
        QVERIFY(server.listening());
        server.reply = [&](const QJsonObject &, int index) {
            return index < 4 ? completion({}, {nativeCall(QStringLiteral("invalid_%1").arg(index), "unknown_tool", {})}) : completion("Unexpected unbounded retries.");
        };
        QSignalSpy finished(engine.get(), &AgentEngine::finished), errors(engine.get(), &AgentEngine::error);
        QSignalSpy executed(tools.get(), &ToolBroker::completed);
        start(server, settings("native", false, 1));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(executed.count(), 0);
        QCOMPARE(errors.count(), 1);
        QCOMPARE(server.requests.size(), 2);
        const auto results = toolResults(engine->history());
        QCOMPARE(code(results.value("invalid_0")), QStringLiteral("invalid_tool_call"));
        QCOMPARE(code(results.value("invalid_1")), QStringLiteral("step_limit"));
        QVERIFY2(historyProblem(engine->history()).isEmpty(), qPrintable(historyProblem(engine->history())));
    }

    void restoringInterruptedHistoryAddsMissingResults()
    {
        const QJsonArray initial{QJsonObject{{"role", "user"}, {"content", "Old request"}},
            QJsonObject{{"role", "assistant"}, {"content", ""}, {"tool_calls", QJsonArray{
                nativeCall("old_read", "files_read", {{"path", sample}}), nativeCall("old_write", "files_write", {{"path", sample}, {"content", "never confirmed"}})}}},
            QJsonObject{{"role", "tool"}, {"tool_call_id", "old_read"}, {"name", "files_read"}, {"content", "{\"ok\":true,\"content\":\"historical text\"}"}},
            QJsonObject{{"role", "user"}, {"content", "Newer saved request"}}};
        engine->setHistory(initial);
        QVERIFY2(historyProblem(engine->history()).isEmpty(), qPrintable(historyProblem(engine->history())));
        const auto results = toolResults(engine->history());
        QVERIFY(results.value("old_read").value("ok").toBool());
        QCOMPARE(code(results.value("old_write")), QStringLiteral("interrupted"));
        ModelServer server;
        QVERIFY(server.listening());
        server.reply = [](const QJsonObject &, int) { return completion("Restored safely."); };
        QSignalSpy finished(engine.get(), &AgentEngine::finished), executed(tools.get(), &ToolBroker::completed);
        start(server);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(executed.count(), 0);
        QCOMPARE(get(sample), QByteArray("original text\n"));
        auto context = server.requests.first().value("messages").toArray();
        context.removeFirst(); // System instructions precede native history.
        QVERIFY2(historyProblem(context).isEmpty(), qPrintable(historyProblem(context)));
    }

    void oldQueuedTurnCannotExecuteInNewTurn()
    {
        ModelServer server;
        QVERIFY(server.listening());
        const QString pending = workspace + QStringLiteral("/pending.txt");
        server.reply = [&](const QJsonObject &, int index) {
            return index == 0 ? completion({}, {nativeCall("old_read", "files_read", {{"path", sample}}),
                nativeCall("old_write", "files_write", {{"path", pending}, {"content", "must not run"}})}) : completion("New turn answered.");
        };
        bool restarted = false;
        connect(tools.get(), &ToolBroker::completed, engine.get(), [&](const QString &, const QJsonObject &) {
            if (restarted) return;
            restarted = true;
            engine->cancel();
            engine->start("New turn", settings("native", false), "fake-model", server.endpoint(), {}, {});
        });
        QSignalSpy finished(engine.get(), &AgentEngine::finished), executed(tools.get(), &ToolBroker::completed);
        start(server, settings("native", false));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 5000);
        QTest::qWait(80);
        QCOMPARE(executed.count(), 1);
        QCOMPARE(server.requests.size(), 2);
        QVERIFY(!QFileInfo::exists(pending));
        QCOMPARE(engine->history().last().toObject().value("content").toString(), QStringLiteral("New turn answered."));
        QVERIFY2(historyProblem(engine->history()).isEmpty(), qPrintable(historyProblem(engine->history())));
    }

private:
    void start(ModelServer &server, const QVariantMap &options = settings())
    {
        engine->start("Test the requested action.", options, "fake-model", server.endpoint(), {}, {});
    }
    std::unique_ptr<QTemporaryDir> temporary;
    std::unique_ptr<ChatModel> model;
    std::unique_ptr<ToolBroker> tools;
    std::unique_ptr<AgentEngine> engine;
    QString workspace, privateData, sample;
};

QTEST_GUILESS_MAIN(AgentEngineTests)
#include "AgentEngineTests.moc"
