#include <QtTest>

#include "util/format.h"
#include "util/memory.h"

class TestUtil : public QObject
{
    Q_OBJECT

private slots:
    void formatsSizes();
    void reportsProcessMemory();
};

void TestUtil::formatsSizes()
{
    // Same rules as the reference's format_size.
    QCOMPARE(util::formatSize(0), QStringLiteral("0 B"));
    QCOMPARE(util::formatSize(1023), QStringLiteral("1023 B"));
    QCOMPARE(util::formatSize(1024), QStringLiteral("1.0 KB"));
    QCOMPARE(util::formatSize(1536), QStringLiteral("1.5 KB"));
    QCOMPARE(util::formatSize(1024 * 1024), QStringLiteral("1.0 MB"));
    QCOMPARE(util::formatSize(15 * 1024 * 1024), QStringLiteral("15.0 MB"));
    QCOMPARE(util::formatSize(3LL * 1024 * 1024 * 1024), QStringLiteral("3.0 GB"));
    QCOMPARE(util::formatSize(2LL * 1024 * 1024 * 1024 * 1024), QStringLiteral("2.0 TB"));
}

void TestUtil::reportsProcessMemory()
{
    const qint64 rss = util::processRssBytes();
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    QVERIFY(rss > 0);
    // Sanity bound: the test process alone is well above 1 MB.
    QVERIFY(rss > 1024 * 1024);
#else
    QCOMPARE(rss, qint64(0));
#endif
}

QTEST_GUILESS_MAIN(TestUtil)

#include "test_util.moc"
