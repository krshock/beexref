#include <QBuffer>
#include <QImage>
#include <QMimeData>
#include <QStringEncoder>
#include <QtTest>

#include "ui/drop.h"

namespace {

QByteArray utf16le(const QString &text)
{
    QStringEncoder encoder(QStringEncoder::Utf16LE);
    return encoder(text);
}

} // namespace

class TestDrop : public QObject
{
    Q_OBJECT

private slots:
    void acceptsUsableFormats();
    void rejectsUnusablePayloads();
    void standardFileUrls();
    void uriListParserSkipsComments();
    void standardImageData();
    void htmlImageUrl();
    void plainTextUrl();
    void plainTextIsNotADrop();
    void chromiumCustomPayloadPrefersImageUrl();
    void webkitEmptyPayloadIsAcceptedProvisionally();
    void webkitPayloadFindsUrl();
};

void TestDrop::acceptsUsableFormats()
{
    QMimeData text;
    text.setText(QStringLiteral("hello"));
    QVERIFY(ui::dropAccepts(text));

    QMimeData image;
    image.setImageData(QImage(2, 2, QImage::Format_ARGB32));
    QVERIFY(ui::dropAccepts(image));

    QMimeData custom;
    custom.setData(QStringLiteral("chromium/x-web-custom-data"), QByteArrayLiteral("x"));
    QVERIFY(ui::dropAccepts(custom));
}

void TestDrop::rejectsUnusablePayloads()
{
    QMimeData empty;
    QVERIFY(!ui::dropAccepts(empty));
    QCOMPARE(ui::inspectDrop(empty).kind, ui::DropKind::None);
    QCOMPARE(ui::inspectDrop(empty).message, QString::fromLatin1(ui::kNoDropMessage));
}

void TestDrop::standardFileUrls()
{
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(QStringLiteral("/tmp/a.png"))});
    const ui::DropResult result = ui::inspectDrop(mime);
    QCOMPARE(result.kind, ui::DropKind::Urls);
    QVERIFY(result.accepted);
    QCOMPARE(result.urls.size(), 1);
}

void TestDrop::uriListParserSkipsComments()
{
    // Some sources deliver the raw uri-list while Qt's parser comes up
    // empty; the reference parses it manually and so do we.
    const QList<QUrl> urls = ui::parseUriList(
        QByteArrayLiteral("#comment\nfile:///tmp/a.png\n\nfile:///tmp/b.jpg\n"));
    QCOMPARE(urls.size(), 2);
    QCOMPARE(urls.at(0).toLocalFile(), QStringLiteral("/tmp/a.png"));
    QCOMPARE(urls.at(1).toLocalFile(), QStringLiteral("/tmp/b.jpg"));
}

void TestDrop::standardImageData()
{
    QImage image(3, 2, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QMimeData mime;
    mime.setImageData(image);
    const ui::DropResult result = ui::inspectDrop(mime);
    QCOMPARE(result.kind, ui::DropKind::Image);
    QCOMPARE(result.image.size(), QSize(3, 2));
}

void TestDrop::htmlImageUrl()
{
    QMimeData mime;
    mime.setHtml(QStringLiteral("<img src=\"https://example.com/pic.png\">"));
    const ui::DropResult result = ui::inspectDrop(mime);
    QCOMPARE(result.kind, ui::DropKind::Urls);
    QCOMPARE(result.urls.first().toString(), QStringLiteral("https://example.com/pic.png"));
}

void TestDrop::plainTextUrl()
{
    QMimeData mime;
    mime.setText(QStringLiteral("  https://example.com/pic.jpg  "));
    const ui::DropResult result = ui::inspectDrop(mime);
    QCOMPARE(result.kind, ui::DropKind::Urls);
    QCOMPARE(result.urls.first().toString(), QStringLiteral("https://example.com/pic.jpg"));
}

void TestDrop::plainTextIsNotADrop()
{
    QMimeData mime;
    mime.setText(QStringLiteral("just a note"));
    const ui::DropResult result = ui::inspectDrop(mime);
    QCOMPARE(result.kind, ui::DropKind::None);
    QCOMPARE(result.message, QString::fromLatin1(ui::kNoDropMessage));
}

void TestDrop::chromiumCustomPayloadPrefersImageUrl()
{
    QMimeData mime;
    mime.setData(QStringLiteral("chromium/x-web-custom-data"),
                 utf16le(QStringLiteral(
                     "https://example.com/page https://cdn.example.com/pic.webp?x=1")));
    mime.setData(QStringLiteral("chromium/x-renderer-taint"),
                 QByteArrayLiteral("source-domain"));
    const ui::DropResult result = ui::inspectDrop(mime);
    QCOMPARE(result.kind, ui::DropKind::Urls);
    QCOMPARE(result.urls.first().toString(),
             QStringLiteral("https://cdn.example.com/pic.webp?x=1"));
}

void TestDrop::webkitEmptyPayloadIsAcceptedProvisionally()
{
    QMimeData mime;
    mime.setData(QStringLiteral("org.webkitgtk.WebKit.custom-pasteboard-data"), QByteArray());
    const ui::DropResult result = ui::inspectDrop(mime);
    QCOMPARE(result.kind, ui::DropKind::None);
    QVERIFY(result.accepted);
    QCOMPARE(result.message, QString::fromLatin1(ui::kWebKitEmptyMessage));
}

void TestDrop::webkitPayloadFindsUrl()
{
    QMimeData mime;
    mime.setData(QStringLiteral("org.webkitgtk.WebKit.custom-pasteboard-data"),
                 QByteArrayLiteral("noise https://example.com/a.png"));
    const ui::DropResult result = ui::inspectDrop(mime);
    QCOMPARE(result.kind, ui::DropKind::Urls);
    QCOMPARE(result.urls.first().toString(), QStringLiteral("https://example.com/a.png"));
}

QTEST_GUILESS_MAIN(TestDrop)

#include "test_drop.moc"
