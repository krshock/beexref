#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

namespace ui {

// Fetches remote images with the browser user agent the app uses,
// so sites that block default download clients work. pinterest.com page
// URLs resolve to their first <img> source.
class Downloader : public QObject
{
    Q_OBJECT

public:
    explicit Downloader(QObject *parent = nullptr);

    // requestId is echoed back so callers can map a result to a drop
    // position.
    void fetch(quint64 requestId, const QUrl &url);

signals:
    void finished(quint64 requestId, const QByteArray &bytes, const QString &source);
    void failed(quint64 requestId, const QString &source, const QString &error);

private:
    void fetchPage(quint64 requestId, const QUrl &url);
    void fetchImage(quint64 requestId, const QUrl &url);

    QNetworkAccessManager *manager_ = nullptr;
};

} // namespace ui
