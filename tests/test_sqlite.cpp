#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <thread>

#include "board/sqlite.h"

namespace {

qint64 countRows(board::Connection &db)
{
    auto statement = db.prepare(QStringLiteral("SELECT count(*) FROM t"));
    if (!statement)
        return -1;
    auto row = statement.value().step();
    if (!row || !row.value())
        return -1;
    return statement.value().columnInt64(0);
}

} // namespace

class TestSqlite : public QObject
{
    Q_OBJECT

private slots:
    void createAndRoundTrip();
    void bindTypes();
    void readonlyRejectsWrites();
    void transactionRollback();
    void concurrentReads();
    void useAfterCloseIsAnError();
};

void TestSqlite::createAndRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto db = board::Connection::open(dir.filePath(QStringLiteral("test.beex")),
                                    board::Connection::OpenMode::Create);
    QVERIFY(db.isOk());
    QVERIFY(db.value().isOpen());
    QCOMPARE(db.value().path(), dir.filePath(QStringLiteral("test.beex")));

    QVERIFY(db.value()
                .exec(QStringLiteral("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)"))
                .isOk());
    QVERIFY(db.value()
                .exec(QStringLiteral("INSERT INTO t (name) VALUES ('a'), ('b')"))
                .isOk());
    QCOMPARE(db.value().lastInsertRowId(), qint64(2));
    QCOMPARE(db.value().changes(), 2);

    auto statement = db.value().prepare(QStringLiteral("SELECT id, name FROM t ORDER BY id"));
    QVERIFY(statement.isOk());
    auto row = statement.value().step();
    QVERIFY(row.isOk());
    QVERIFY(row.value());
    QCOMPARE(statement.value().columnInt64(0), qint64(1));
    QCOMPARE(statement.value().columnText(1), QStringLiteral("a"));

    row = statement.value().step();
    QVERIFY(row.isOk());
    QVERIFY(row.value());
    QCOMPARE(statement.value().columnText(1), QStringLiteral("b"));

    row = statement.value().step();
    QVERIFY(row.isOk());
    QVERIFY(!row.value());

    // Errors carry a message and the file path.
    auto broken = db.value().prepare(QStringLiteral("SELECT * FROM missing_table"));
    QVERIFY(!broken.isOk());
    QVERIFY(!broken.error().message.isEmpty());
    QCOMPARE(broken.error().path, dir.filePath(QStringLiteral("test.beex")));
}

void TestSqlite::bindTypes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto db = board::Connection::open(dir.filePath(QStringLiteral("types.beex")),
                                    board::Connection::OpenMode::Create);
    QVERIFY(db.isOk());
    QVERIFY(db.value()
                .exec(QStringLiteral("CREATE TABLE t (i INTEGER, d REAL, s TEXT, b BLOB, n TEXT)"))
                .isOk());

    const QByteArray blob("\x00\x01\x02\xff", 4);
    {
        auto insert = db.value().prepare(QStringLiteral("INSERT INTO t VALUES (?, ?, ?, ?, ?)"));
        QVERIFY(insert.isOk());
        QVERIFY(insert.value().bind(1, qint64(42)).isOk());
        QVERIFY(insert.value().bind(2, 1.5).isOk());
        QVERIFY(insert.value().bind(3, QStringLiteral("text")).isOk());
        // The blob is bound by reference and must stay alive until the
        // statement finishes.
        QVERIFY(insert.value().bind(4, blob).isOk());
        QVERIFY(insert.value().bind(5, nullptr).isOk());
        QVERIFY(insert.value().exec().isOk());
    }

    auto select = db.value().prepare(QStringLiteral("SELECT i, d, s, b, n FROM t"));
    QVERIFY(select.isOk());
    auto row = select.value().step();
    QVERIFY(row.isOk());
    QVERIFY(row.value());
    QCOMPARE(select.value().columnInt64(0), qint64(42));
    QCOMPARE(select.value().columnDouble(1), 1.5);
    QCOMPARE(select.value().columnText(2), QStringLiteral("text"));
    QCOMPARE(select.value().columnBlob(3), blob);
    QVERIFY(select.value().isNull(4));
}

void TestSqlite::readonlyRejectsWrites()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.filePath(QStringLiteral("readonly.beex"));
    {
        auto db = board::Connection::open(path, board::Connection::OpenMode::Create);
        QVERIFY(db.isOk());
        QVERIFY(db.value().exec(QStringLiteral("CREATE TABLE t (id INTEGER)")).isOk());
        QVERIFY(db.value().exec(QStringLiteral("INSERT INTO t VALUES (1)")).isOk());
    }

    auto db = board::Connection::open(path, board::Connection::OpenMode::ReadOnly);
    QVERIFY(db.isOk());
    QCOMPARE(countRows(db.value()), qint64(1));
    QVERIFY(!db.value().exec(QStringLiteral("INSERT INTO t VALUES (2)")).isOk());
    QCOMPARE(countRows(db.value()), qint64(1));

    auto missing = board::Connection::open(dir.filePath(QStringLiteral("missing.beex")),
                                         board::Connection::OpenMode::ReadOnly);
    QVERIFY(!missing.isOk());
    QVERIFY(!missing.error().message.isEmpty());
}

void TestSqlite::transactionRollback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto db = board::Connection::open(dir.filePath(QStringLiteral("tx.beex")),
                                    board::Connection::OpenMode::Create);
    QVERIFY(db.isOk());
    QVERIFY(db.value().exec(QStringLiteral("CREATE TABLE t (id INTEGER)")).isOk());

    {
        auto tx = board::Transaction::begin(db.value());
        QVERIFY(tx.isOk());
        QVERIFY(db.value().exec(QStringLiteral("INSERT INTO t VALUES (1)")).isOk());
        // No commit: the guard rolls back.
    }
    QCOMPARE(countRows(db.value()), qint64(0));

    {
        auto tx = board::Transaction::begin(db.value());
        QVERIFY(tx.isOk());
        QVERIFY(db.value().exec(QStringLiteral("INSERT INTO t VALUES (2)")).isOk());
        QVERIFY(tx.value().commit().isOk());
    }
    QCOMPARE(countRows(db.value()), qint64(1));
}

void TestSqlite::concurrentReads()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto db = board::Connection::open(dir.filePath(QStringLiteral("threads.beex")),
                                    board::Connection::OpenMode::Create);
    QVERIFY(db.isOk());
    QVERIFY(db.value().exec(QStringLiteral("CREATE TABLE t (id INTEGER PRIMARY KEY, v INTEGER)"))
                .isOk());
    {
        auto insert = db.value().prepare(QStringLiteral("INSERT INTO t VALUES (?, ?)"));
        QVERIFY(insert.isOk());
        for (int i = 1; i <= 100; ++i) {
            QVERIFY(insert.value().bind(1, qint64(i)).isOk());
            QVERIFY(insert.value().bind(2, qint64(i * 10)).isOk());
            QVERIFY(insert.value().exec().isOk());
            QVERIFY(insert.value().reset().isOk());
        }
    }

    std::atomic<int> mismatches{0};
    const auto work = [&db, &mismatches]() {
        for (int i = 0; i < 200; ++i) {
            const qint64 id = (i % 100) + 1;
            auto statement = db.value().prepare(QStringLiteral("SELECT v FROM t WHERE id=?"));
            if (!statement || !statement.value().bind(1, id).isOk()) {
                ++mismatches;
                continue;
            }
            auto row = statement.value().step();
            if (!row || !row.value() || statement.value().columnInt64(0) != id * 10)
                ++mismatches;
        }
    };

    std::thread first(work);
    std::thread second(work);
    first.join();
    second.join();
    QCOMPARE(mismatches.load(), 0);
}

void TestSqlite::useAfterCloseIsAnError()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto db = board::Connection::open(dir.filePath(QStringLiteral("close.beex")),
                                      board::Connection::OpenMode::Create);
    QVERIFY(db.isOk());
    QVERIFY(db.value().exec(QStringLiteral("CREATE TABLE t (id INTEGER PRIMARY KEY)")).isOk());
    QVERIFY(db.value().exec(QStringLiteral("INSERT INTO t VALUES (1)")).isOk());

    auto statement = db.value().prepare(QStringLiteral("SELECT id FROM t"));
    QVERIFY(statement.isOk());
    auto first = statement.value().step();
    QVERIFY(first.isOk());
    QVERIFY(first.value());
    QCOMPARE(statement.value().columnInt64(0), qint64(1));

    db.value().close();
    QVERIFY(!db.value().isOpen());
    QVERIFY(!db.value().exec(QStringLiteral("SELECT 1")).isOk());

    auto afterClose = db.value().prepare(QStringLiteral("SELECT 1"));
    QVERIFY(!afterClose.isOk());
    QVERIFY(afterClose.error().message.contains(QStringLiteral("closed")));

    // Statements keep the connection state alive and fail cleanly
    // instead of touching a closed handle.
    auto row = statement.value().step();
    QVERIFY(!row.isOk());
    QVERIFY(row.error().message.contains(QStringLiteral("closed")));
    QVERIFY(!statement.value().reset().isOk());

    statement.value() = {};
}

QTEST_GUILESS_MAIN(TestSqlite)

#include "test_sqlite.moc"
