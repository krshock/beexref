#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include "ui/downloader.h"

namespace {

QByteArray makePng()
{
    QImage image(8, 4, QImage::Format_ARGB32);
    image.fill(Qt::darkYellow);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// Serves one canned response per connection and records the request.
class TinyServer : public QTcpServer
{
public:
    explicit TinyServer(QObject *parent = nullptr)
        : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, &TinyServer::serve);
    }

    QByteArray lastRequest;
    QByteArray responseBody;
    QByteArray responseStatus = QByteArrayLiteral("200 OK");

private:
    void serve()
    {
        QTcpSocket *socket = nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            lastRequest += socket->readAll();
            if (!lastRequest.contains("\r\n\r\n"))
                return;
            QByteArray response = QByteArrayLiteral("HTTP/1.1 ") + responseStatus
                + QByteArrayLiteral("\r\nContent-Type: image/png\r\nContent-Length: ")
                + QByteArray::number(responseBody.size())
                + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + responseBody;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
    }
};

} // namespace

class TestDownloader : public QObject
{
    Q_OBJECT

private slots:
    void fetchesWithBrowserUserAgent();
    void reportsHttpErrors();
};

void TestDownloader::fetchesWithBrowserUserAgent()
{
    TinyServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.responseBody = makePng();

    ui::Downloader downloader;
    QSignalSpy finished(&downloader, &ui::Downloader::finished);
    QSignalSpy failed(&downloader, &ui::Downloader::failed);

    const QString url =
        QStringLiteral("http://127.0.0.1:%1/image.png").arg(server.serverPort());
    downloader.fetch(7, QUrl(url));

    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(failed.count(), 0);
    const QList<QVariant> arguments = finished.takeFirst();
    QCOMPARE(arguments.at(0).toULongLong(), quint64(7));
    QCOMPARE(arguments.at(1).toByteArray(), server.responseBody);
    QCOMPARE(arguments.at(2).toString(), url);
    QVERIFY(server.lastRequest.contains("Chrome/120.0.0.0"));
}

void TestDownloader::reportsHttpErrors()
{
    TinyServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.responseStatus = QByteArrayLiteral("404 Not Found");

    ui::Downloader downloader;
    QSignalSpy finished(&downloader, &ui::Downloader::finished);
    QSignalSpy failed(&downloader, &ui::Downloader::failed);

    downloader.fetch(3, QUrl(QStringLiteral("http://127.0.0.1:%1/missing.png")
                                 .arg(server.serverPort())));

    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(finished.count(), 0);
    QCOMPARE(failed.first().at(0).toULongLong(), quint64(3));
}

QTEST_MAIN(TestDownloader)

#include "test_downloader.moc"
