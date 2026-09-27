#include <QtTest>

#include "doc/item.h"
#include "doc/source.h"

class TestItem : public QObject
{
    Q_OBJECT

private slots:
    void defaults();
    void originalSizeRoundTrip();
    void textAccessors();
    void opacityGrayscaleCrop();
    void uuidIsStable();
    void createCopyKeepsStateAndSharesSource();
    void knownTypesAndFactory();
};

void TestItem::defaults()
{
    const doc::Item item(doc::kTypePixmap);
    QVERIFY(item.isPixmap());
    QVERIFY(!item.isText());
    QCOMPARE(item.id, qint64(0));
    QCOMPARE(item.scale, 1.0);
    QCOMPARE(item.flip, 1.0);
    QCOMPARE(item.opacity(), 1.0);
    QVERIFY(!item.grayscale());
    QVERIFY(!item.crop().isValid());
    QVERIFY(!item.hasSource());
    QVERIFY(!item.originalSize().isValid());
}

void TestItem::originalSizeRoundTrip()
{
    doc::Item item(doc::kTypePixmap);
    item.setOriginalSize(QSize(300, 200));
    QCOMPARE(item.originalSize(), QSize(300, 200));
    QCOMPARE(item.data.value(QStringLiteral("original_width")).toInt(), 300);
    QCOMPARE(item.data.value(QStringLiteral("original_height")).toInt(), 200);
}

void TestItem::textAccessors()
{
    doc::Item item(doc::kTypeText);
    QVERIFY(item.isText());
    QCOMPARE(item.text(), QString());
    item.setText(QStringLiteral("hello"));
    QCOMPARE(item.text(), QStringLiteral("hello"));
    QCOMPARE(item.data.value(QStringLiteral("text")).toString(), QStringLiteral("hello"));
}

void TestItem::opacityGrayscaleCrop()
{
    doc::Item item(doc::kTypePixmap);
    item.setOpacity(0.4);
    QCOMPARE(item.opacity(), 0.4);
    item.setGrayscale(true);
    QVERIFY(item.grayscale());

    item.setCrop(QRectF(10, 20, 30, 40));
    QCOMPARE(item.crop(), QRectF(10, 20, 30, 40));
    const QJsonValue stored = item.data.value(QStringLiteral("crop"));
    QVERIFY(stored.isArray());
    QCOMPARE(stored.toArray().size(), 4);
    QCOMPARE(stored.toArray().at(0).toDouble(), 10.0);

    item.setCrop(QRectF());
    QVERIFY(!item.crop().isValid());
    QVERIFY(!item.data.contains(QStringLiteral("crop")));
}

void TestItem::uuidIsStable()
{
    doc::Item item(doc::kTypePixmap);
    const QString first = item.ensureUuid();
    QCOMPARE(first.size(), 32);
    QVERIFY(!first.contains(QLatin1Char('-')));
    QCOMPARE(item.ensureUuid(), first);

    item.uuid = QStringLiteral("fixed");
    QCOMPARE(item.ensureUuid(), QStringLiteral("fixed"));
}

void TestItem::createCopyKeepsStateAndSharesSource()
{
    doc::Item item(doc::kTypePixmap);
    item.id = 7;
    item.uuid = QStringLiteral("original");
    item.x = 12;
    item.y = 34;
    item.z = 3;
    item.scale = 0.75;
    item.rotation = 90;
    item.flip = -1;
    item.data.insert(QStringLiteral("filename"), QStringLiteral("a.png"));
    item.meta.insert(QStringLiteral("author"), QStringLiteral("me"));
    item.setOriginalSize(QSize(300, 200));
    item.source = std::make_shared<doc::BytesSource>(QByteArrayLiteral("bytes"));

    const doc::ItemPtr copy = item.createCopy();
    QCOMPARE(copy->id, qint64(0));
    QCOMPARE(copy->uuid.size(), 32);
    QVERIFY(copy->uuid != item.uuid);
    QCOMPARE(copy->x, 12.0);
    QCOMPARE(copy->scale, 0.75);
    QCOMPARE(copy->rotation, 90.0);
    QCOMPARE(copy->flip, -1.0);
    QCOMPARE(copy->data, item.data);
    QCOMPARE(copy->meta, item.meta);
    QCOMPARE(copy->originalSize(), QSize(300, 200));
    QCOMPARE(copy->source, item.source);
}

void TestItem::knownTypesAndFactory()
{
    QVERIFY(doc::isKnownType(QStringLiteral("pixmap")));
    QVERIFY(doc::isKnownType(QStringLiteral("text")));
    QVERIFY(doc::isKnownType(QStringLiteral("error")));
    QVERIFY(!doc::isKnownType(QStringLiteral("future")));

    // Unknown types are still created: a board from a newer build loads
    // and round-trips.
    const doc::ItemPtr item = doc::createItem(QStringLiteral("future"));
    QVERIFY(item);
    QCOMPARE(item->type, QStringLiteral("future"));
    QVERIFY(!item->isPixmap());
    QVERIFY(!item->isText());
    QVERIFY(!item->isError());
}

QTEST_GUILESS_MAIN(TestItem)

#include "test_doc_item.moc"
