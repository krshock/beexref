#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "board/sqlite.h"
#include "cache/session_cache.h"

class TestSessionCache : public QObject
{
    Q_OBJECT

private slots:
    void putGetRemoveAndDeleteOnDestruction();
    void unavailableCacheIsANoop();
    void recreatesOnVersionMismatch();
    void failedOpenDisablesTheCache();
    void sweepsStaleFiles();
};

void TestSessionCache::putGetRemoveAndDeleteOnDestruction()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString path;
    {
        auto cache = cache::SessionCache::create(dir.path());
        QVERIFY(cache->isAvailable());
        path = cache->path();
        // The connection is lazy: nothing is on disk until first use.
        QVERIFY(!QFile::exists(path));

        QVERIFY(cache->put(QStringLiteral("lod"), QStringLiteral("k1"),
                           QStringLiteral("png"), QByteArrayLiteral("payload")));
        QVERIFY(QFile::exists(path));
        const auto value = cache->get(QStringLiteral("lod"), QStringLiteral("k1"));
        QVERIFY(value.has_value());
        QCOMPARE(*value, QByteArrayLiteral("payload"));
        QVERIFY(cache->fileBytes() > 0);

        QVERIFY(cache->remove(QStringLiteral("lod"), QStringLiteral("k1")));
        QVERIFY(!cache->get(QStringLiteral("lod"), QStringLiteral("k1")).has_value());
    }
    // The session file goes away with the cache.
    QVERIFY(!QFile::exists(path));
}

void TestSessionCache::unavailableCacheIsANoop()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString blocker = dir.filePath(QStringLiteral("blocker"));
    {
        QFile file(blocker);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
    }

    auto cache = cache::SessionCache::create(blocker + QStringLiteral("/sub"));
    QVERIFY(!cache->isAvailable());
    QVERIFY(!cache->put(QStringLiteral("lod"), QStringLiteral("k"), QStringLiteral("png"),
                        QByteArrayLiteral("d")));
    QVERIFY(!cache->get(QStringLiteral("lod"), QStringLiteral("k")).has_value());
    QCOMPARE(cache->fileBytes(), qint64(0));
}

void TestSessionCache::failedOpenDisablesTheCache()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // The parent directory is missing, so the lazy open fails.
    const QString path = dir.filePath(QStringLiteral("missing/cache.db"));

    auto cache = cache::SessionCache::createAt(path);
    QVERIFY(cache->isAvailable());
    QVERIFY(!cache->put(QStringLiteral("lod"), QStringLiteral("k"), QStringLiteral("png"),
                        QByteArrayLiteral("d")));
    // A failed open disables the cache for the rest of the session.
    QVERIFY(!cache->isAvailable());
    QVERIFY(!cache->put(QStringLiteral("lod"), QStringLiteral("k"), QStringLiteral("png"),
                        QByteArrayLiteral("d")));
    QVERIFY(!cache->get(QStringLiteral("lod"), QStringLiteral("k")).has_value());
    QCOMPARE(cache->fileBytes(), qint64(0));
}

void TestSessionCache::recreatesOnVersionMismatch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("session-test.cachedb"));

    // A file from another cache version: disposable, so it is recreated.
    {
        auto db = board::Connection::open(path, board::Connection::OpenMode::Create);
        QVERIFY(db.isOk());
        QVERIFY(db.value().exec(QStringLiteral("PRAGMA user_version=99")).isOk());
        QVERIFY(db.value().exec(QStringLiteral(
            "CREATE TABLE blobs (kind TEXT, key TEXT, format TEXT, data BLOB)")).isOk());
        QVERIFY(db.value().exec(QStringLiteral(
            "INSERT INTO blobs VALUES ('lod', 'old', 'png', 'x')")).isOk());
    }

    auto cache = cache::SessionCache::createAt(path);
    QVERIFY(cache->isAvailable());
    QVERIFY(!cache->get(QStringLiteral("lod"), QStringLiteral("old")).has_value());
    QVERIFY(cache->put(QStringLiteral("lod"), QStringLiteral("new"),
                       QStringLiteral("png"), QByteArrayLiteral("y")));
}

void TestSessionCache::sweepsStaleFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString live = dir.filePath(
        QStringLiteral("session-%1-abcd.cachedb").arg(QCoreApplication::applicationPid()));
    const QString dead = dir.filePath(QStringLiteral("session-2147483646-abcd.cachedb"));
    const QString unparsable = dir.filePath(QStringLiteral("session-garbage-abcd.cachedb"));
    for (const QString &path : {live, dead, unparsable}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
    }

    cache::SessionCache::sweepStale(dir.path());
    QVERIFY(QFile::exists(live));
    QVERIFY(!QFile::exists(dead));
    QVERIFY(!QFile::exists(unparsable));
}

QTEST_GUILESS_MAIN(TestSessionCache)

#include "test_session_cache.moc"
