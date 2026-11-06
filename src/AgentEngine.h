#pragma once
#include "ChatModel.h"
#include "LocalApiClient.h"
#include "ToolBroker.h"
#include <QQueue>
namespace titan {
class AgentEngine final : public QObject {
    Q_OBJECT
  public:
    AgentEngine(ChatModel *model, ToolBroker *tools, QObject *parent = nullptr);
    bool busy() const { return busy_; }
    QString status() const { return status_; }
    QJsonArray history() const { return history_; }
    void setHistory(QJsonArray history);
    void start(const QString &prompt, const QVariantMap &settings, const QString &model, const QUrl &endpoint,
               const QString &key, const QString &thinkingStyle);
    void cancel();
    void approve(bool allowed);
  signals:
    void changed();
    void finished();
    void error(QString message);
    void approvalRequested(QVariantMap request);

  private:
    struct Call {
        QString id, name;
        QJsonObject arguments;
    };
    ChatModel *model_;
    ToolBroker *tools_;
    LocalApiClient client_;
    QJsonArray history_;
    QVariantMap settings_;
    QQueue<Call> queue_;
    Call current_;
    QString modelId_, apiKey_, thinkingStyle_, assistantId_, toolMessage_, status_;
    QUrl endpoint_;
    bool busy_ = false, jsonMode_ = false, toolRunning_ = false, waitingApproval_ = false,
         cancelRequested_ = false, fallbackUsed_ = false;
    int steps_ = 0;
    quint64 turn_ = 0;
    void requestNext();
    void modelCompleted(const QJsonObject &result);
    void nextTool();
    void executeCurrent();
    void toolCompleted(const QString &id, const QJsonObject &result);
    void settlePendingCalls();
    void fail(const QString &message);
    void finish();
    QJsonArray contextMessages() const;
    QString systemPrompt() const;
};
} // namespace titan
