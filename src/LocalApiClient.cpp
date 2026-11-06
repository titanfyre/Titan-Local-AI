#include "LocalApiClient.h"
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QUuid>
namespace titan {
LocalApiClient::LocalApiClient(QObject *p) : QObject(p) {
    network_.setProxy(QNetworkProxy::NoProxy);
    timeout_.setSingleShot(true);
    timeout_.setInterval(180000);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if (reply_) {
            cancel();
            emit failed("Le modèle ne répond plus. Réessayez avec un contexte plus court ou consultez le "
                        "moteur local.");
        }
    });
}
bool LocalApiClient::isLocalEndpoint(const QUrl &url) {
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https") || !url.userInfo().isEmpty() ||
        !url.fragment().isEmpty())
        return false;
    return url.host().compare("localhost", Qt::CaseInsensitive) == 0 || QHostAddress(url.host()).isLoopback();
}
QUrl LocalApiClient::completionUrl(const QUrl &base) {
    QUrl u = base;
    QString p = u.path();
    while (p.endsWith('/'))
        p.chop(1);
    if (!p.endsWith("/chat/completions")) {
        if (!p.endsWith("/v1"))
            p += "/v1";
        p += "/chat/completions";
    }
    u.setPath(p);
    u.setQuery({});
    return u;
}
void LocalApiClient::request(const QUrl &base, QJsonObject payload, const QString &apiKey) {
    if (reply_) {
        emit failed("Une génération est déjà en cours.");
        return;
    }
    if (!isLocalEndpoint(base)) {
        emit failed("L’adresse du modèle doit être locale : localhost, 127.0.0.1 ou ::1.");
        return;
    }
    buffer_.clear();
    raw_.clear();
    text_.clear();
    reasoning_.clear();
    calls_.clear();
    usage_ = {};
    streaming_ = false;
    payload["stream"] = true;
    QNetworkRequest req(completionUrl(base));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    if (!apiKey.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
    auto *current = network_.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    reply_ = current;
    timeout_.start();
    connect(current, &QNetworkReply::readyRead, this, [this, current] {
        if (reply_ != current)
            return;
        const auto bytes = current->readAll();
        if (raw_.size() + bytes.size() > 8 * 1024 * 1024) {
            cancel();
            emit failed("La réponse du modèle dépasse la limite de sécurité.");
            return;
        }
        raw_ += bytes;
        buffer_ += bytes;
        timeout_.start();
        consume();
    });
    connect(current, &QNetworkReply::finished, this, [this, current] {
        if (reply_ != current)
            return;
        timeout_.stop();
        const auto remaining = current->readAll();
        raw_ += remaining;
        buffer_ += remaining;
        consume();
        const int status = current->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = current->error();
        const auto message = current->errorString();
        reply_ = nullptr;
        current->deleteLater();
        if (error != QNetworkReply::NoError || status < 200 || status >= 300) {
            auto obj = QJsonDocument::fromJson(raw_).object();
            QString detail = obj.value("error").isObject()
                                 ? obj.value("error").toObject().value("message").toString()
                                 : obj.value("error").toString();
            emit failed(QString("Moteur local (%1) : %2")
                            .arg(status)
                            .arg(detail.isEmpty() ? message : detail.left(1200)));
            return;
        }
        if (!buffer_.trimmed().isEmpty() && buffer_.trimmed().startsWith("data:")) {
            streaming_ = true;
            event(buffer_.trimmed().mid(5).trimmed());
            buffer_.clear();
        }
        if (!streaming_) {
            auto o = QJsonDocument::fromJson(raw_).object();
            if (o.isEmpty()) {
                emit failed("Le serveur local a renvoyé une réponse illisible.");
                return;
            }
            auto choices = o.value("choices").toArray();
            if (choices.isEmpty()) {
                emit failed("Le serveur local n’a renvoyé aucun choix.");
                return;
            }
            acceptDelta(choices.first().toObject().value("message").toObject());
            usage_ = o.value("usage").toObject();
        }
        QJsonArray calls;
        for (auto it = calls_.begin(); it != calls_.end(); ++it) {
            auto c = it.value();
            if (c.value("id").toString().isEmpty())
                c["id"] = "call_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
            c["type"] = "function";
            calls.append(c);
        }
        emit completed(QJsonObject{{"role", "assistant"},
                                   {"content", text_},
                                   {"reasoning_content", reasoning_},
                                   {"tool_calls", calls},
                                   {"usage", usage_}});
    });
}
void LocalApiClient::cancel() {
    timeout_.stop();
    if (!reply_)
        return;
    auto *r = reply_;
    reply_ = nullptr;
    disconnect(r, nullptr, this, nullptr);
    r->abort();
    r->deleteLater();
    emit cancelled();
}
void LocalApiClient::consume() {
    while (true) {
        const auto n = buffer_.indexOf('\n');
        if (n < 0)
            break;
        auto line = buffer_.left(n);
        buffer_.remove(0, n + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.startsWith("data:")) {
            streaming_ = true;
            event(line.mid(5).trimmed());
        }
    }
}
void LocalApiClient::event(const QByteArray &data) {
    if (data == "[DONE]" || data.isEmpty())
        return;
    auto o = QJsonDocument::fromJson(data).object();
    if (o.contains("usage"))
        usage_ = o.value("usage").toObject();
    auto choices = o.value("choices").toArray();
    if (!choices.isEmpty())
        acceptDelta(choices.first().toObject().value("delta").toObject());
}
void LocalApiClient::acceptDelta(const QJsonObject &d) {
    QString content = d.value("content").toString();
    if (d.value("content").isArray())
        for (const auto &v : d.value("content").toArray())
            content += v.toObject().value("text").toString();
    if (!content.isEmpty()) {
        text_ += content;
        emit textDelta(content);
    }
    const auto reasoning = d.value("reasoning_content").toString(d.value("reasoning").toString());
    if (!reasoning.isEmpty()) {
        reasoning_ += reasoning;
        emit reasoningDelta(reasoning);
    }
    int position = 0;
    for (const auto &v : d.value("tool_calls").toArray()) {
        const auto part = v.toObject();
        int i = part.value("index").toInt(position++);
        if (i < 0 || i >= 32)
            continue;
        auto c = calls_.value(i);
        if (part.contains("id"))
            c["id"] = c.value("id").toString() + part.value("id").toString();
        auto f = c.value("function").toObject();
        auto pf = part.value("function").toObject();
        if (pf.contains("name"))
            f["name"] = f.value("name").toString() + pf.value("name").toString();
        if (pf.contains("arguments")) {
            auto a = f.value("arguments").toString() + pf.value("arguments").toString();
            f["arguments"] = a.left(1024 * 1024);
        }
        c["function"] = f;
        calls_[i] = c;
    }
}
} // namespace titan
