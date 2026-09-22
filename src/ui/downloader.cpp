#include "downloader.h"

#include "constants.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace ui {
namespace {

const QRegularExpression &firstImagePattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(<img[^>]+src\s*=\s*["']([^"']+)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

bool isPinterest(const QUrl &url)
{
    const QString host = url.host();
    return host == QLatin1String("pinterest.com") || host.endsWith(QLatin1String(".pinterest.com"));
}

QNetworkRequest makeRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(constants::UserAgent));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

} // namespace

Downloader::Downloader(QObject *parent)
    : QObject(parent)
    , manager_(new QNetworkAccessManager(this))
{
}

void Downloader::fetch(quint64 requestId, const QUrl &url)
{
    if (isPinterest(url))
        fetchPage(requestId, url);
    else
        fetchImage(requestId, url);
}

void Downloader::fetchPage(quint64 requestId, const QUrl &url)
{
    QNetworkReply *reply = manager_->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId, url]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(requestId, url.toString(), reply->errorString());
            return;
        }
        const QString html = QString::fromUtf8(reply->readAll());
        const auto match = firstImagePattern().match(html);
        if (!match.hasMatch()) {
            emit failed(requestId, url.toString(), QStringLiteral("No image found on page"));
            return;
        }
        const QUrl imageUrl = url.resolved(QUrl(match.captured(1).trimmed()));
        if (!imageUrl.isValid()) {
            emit failed(requestId, url.toString(), QStringLiteral("Invalid image URL on page"));
            return;
        }
        fetchImage(requestId, imageUrl);
    });
}

void Downloader::fetchImage(quint64 requestId, const QUrl &url)
{
    QNetworkReply *reply = manager_->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId, url]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(requestId, url.toString(), reply->errorString());
            return;
        }
        const QByteArray bytes = reply->readAll();
        if (bytes.isEmpty()) {
            emit failed(requestId, url.toString(), QStringLiteral("Empty response"));
            return;
        }
        emit finished(requestId, bytes, url.toString());
    });
}

} // namespace ui
