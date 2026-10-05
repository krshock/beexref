#include <QRegularExpression>
#include <QtTest>

#include "constants.h"
#include "util/format.h"
#include "util/memory.h"

class TestUtil : public QObject
{
    Q_OBJECT

private slots:
    void formatsSizes();
    void urlFilenamesAreSafe();
    void appVersionIsSemver();
    void reportsProcessMemory();
};

void TestUtil::appVersionIsSemver()
{
    // The version comes from CMake's project VERSION, optionally
    // decorated by BEEXREF_VERSION_SUFFIX for test and pre-release
    // builds: the base stays explicit MAJOR.MINOR.PATCH so tags and
    // --version stay predictable, and a label keeps SemVer's pre-release
    // or build-metadata shape.
    const QString version = QString::fromLatin1(constants::Version);
    QVERIFY2(QRegularExpression(QStringLiteral(
                                    "^[0-9]+\\.[0-9]+\\.[0-9]+(-[0-9A-Za-z.-]+)?(\\+[0-9A-Za-z.-]+)?$"))
                 .match(version)
                 .hasMatch(),
             qPrintable(version));
}

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

void TestUtil::urlFilenamesAreSafe()
{
    QCOMPARE(util::filenameFromUrl(QUrl(QStringLiteral(
                 "https://crowdworks.jp/attachments/1495102.jpg?height=450&width=600"))),
             QStringLiteral("1495102.jpg"));
    // A dot inside the query must not survive.
    QCOMPARE(util::filenameFromUrl(QUrl(QStringLiteral(
                 "https://instagram.example.net/v/t51/783947786_n.jpg"
                 "?stp=dst-jpg_e35&ig_cache_key=Mzk3%3D%3D.3-ccb7-5"))),
             QStringLiteral("783947786_n.jpg"));
    QCOMPARE(util::filenameFromUrl(QUrl(QStringLiteral(
                 "https://images.unsplash.com/photo-1730389051388-4cc32add20a2"
                 "?q=80&w=1935&auto=format&fit=crop"))),
             QStringLiteral("photo-1730389051388-4cc32add20a2"));

    // Decoded characters no filesystem wants become underscores; an
    // encoded slash only leaves the last path component behind.
    QCOMPARE(util::filenameFromUrl(QUrl(QStringLiteral("https://cdn.example.com/a%3Fb.jpg"))),
             QStringLiteral("a_b.jpg"));
    QCOMPARE(util::filenameFromUrl(QUrl(QStringLiteral("https://x/%2E%2E%2Fetc%2Fpasswd.jpg"))),
             QStringLiteral("passwd.jpg"));

    // No usable segment; the caller falls back to an id-based name.
    QCOMPARE(util::filenameFromUrl(QUrl(QStringLiteral("https://example.com/path/"))), QString());
    QCOMPARE(util::filenameFromUrl(QUrl(QStringLiteral("https://example.com"))), QString());

    // Capped to one path component, without splitting a character.
    const QString capped = util::filenameFromUrl(
        QUrl(QStringLiteral("https://example.com/") + QString(300, QLatin1Char('a'))
             + QStringLiteral(".png")));
    QCOMPARE(capped.toUtf8().size(), 255);
    QCOMPARE(capped, QString(255, QLatin1Char('a')));

    const QString accents = util::filenameFromUrl(
        QUrl(QStringLiteral("https://example.com/") + QString(200, QChar(0x00E9))));
    QCOMPARE(accents.toUtf8().size(), 254);
    QCOMPARE(accents, QString(127, QChar(0x00E9)));
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
