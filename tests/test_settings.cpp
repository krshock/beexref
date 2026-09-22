#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "settings.h"

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void missingFileUsesDefaults();
    void roundTrip();
    void syncFormatIsSorted();
    void recentFilesOrderAndLimit();
    void recentFilesExistingOnly();
    void portablePaths();
};

void TestSettings::missingFileUsesDefaults()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    settings::File file(dir.filePath(QStringLiteral("missing.ini")));
    file.load();

    QVERIFY(!file.contains(QStringLiteral("Items"), QStringLiteral("lod_method")));
    QCOMPARE(file.value(QStringLiteral("Items"), QStringLiteral("lod_method"),
                        QStringLiteral("fixed")),
             QStringLiteral("fixed"));
}

void TestSettings::roundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    settings::File file(dir.filePath(QStringLiteral("settings.ini")));
    file.load();
    file.setValue(QStringLiteral("Items"), QStringLiteral("lod_method"),
                  QStringLiteral("ram_budget"));
    file.setValue(QStringLiteral("Items"), QStringLiteral("arrange_gap"), QStringLiteral("8"));
    file.setValue(QStringLiteral("Save"), QStringLiteral("confirm_close_unsaved"),
                  QStringLiteral("false"));
    QVERIFY(file.sync());
    QVERIFY(QFile::exists(file.path()));

    settings::File reloaded(file.path());
    reloaded.load();
    QCOMPARE(reloaded.value(QStringLiteral("Items"), QStringLiteral("lod_method")),
             QStringLiteral("ram_budget"));
    QCOMPARE(reloaded.value(QStringLiteral("Items"), QStringLiteral("arrange_gap")),
             QStringLiteral("8"));
    QVERIFY(!reloaded.boolValue(QStringLiteral("Save"), QStringLiteral("confirm_close_unsaved"),
                                true));
    QVERIFY(reloaded.boolValue(QStringLiteral("Items"), QStringLiteral("lod_store_thumbnails"),
                               true));
}

void TestSettings::syncFormatIsSorted()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    settings::File file(dir.filePath(QStringLiteral("sorted.ini")));
    file.load();
    file.setValue(QStringLiteral("Save"), QStringLiteral("confirm_close_unsaved"),
                  QStringLiteral("true"));
    file.setValue(QStringLiteral("Items"), QStringLiteral("lod_quality"),
                  QStringLiteral("smooth"));
    file.setValue(QStringLiteral("Items"), QStringLiteral("arrange_gap"), QStringLiteral("0"));
    QVERIFY(file.sync());

    QFile raw(file.path());
    QVERIFY(raw.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QString::fromUtf8(raw.readAll());
    QCOMPARE(text, QStringLiteral("[Items]\n"
                                  "arrange_gap=0\n"
                                  "lod_quality=smooth\n"
                                  "\n"
                                  "[Save]\n"
                                  "confirm_close_unsaved=true\n"
                                  "\n"));
}

void TestSettings::recentFilesOrderAndLimit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    settings::File file(dir.filePath(QStringLiteral("recent.ini")));
    file.load();
    for (int i = 1; i <= 12; ++i)
        file.updateRecentFiles(dir.filePath(QStringLiteral("file%1.beex").arg(i)));

    QStringList files = file.recentFiles();
    QCOMPARE(files.size(), 10);
    QCOMPARE(files.first(), dir.filePath(QStringLiteral("file12.beex")));
    QCOMPARE(files.last(), dir.filePath(QStringLiteral("file3.beex")));
    QCOMPARE(file.value(QStringLiteral("RecentFiles"), QStringLiteral("size")),
             QStringLiteral("10"));

    file.updateRecentFiles(dir.filePath(QStringLiteral("file3.beex")));
    files = file.recentFiles();
    QCOMPARE(files.size(), 10);
    QCOMPARE(files.first(), dir.filePath(QStringLiteral("file3.beex")));
}

void TestSettings::recentFilesExistingOnly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString first = dir.filePath(QStringLiteral("first.beex"));
    const QString second = dir.filePath(QStringLiteral("second.beex"));
    {
        QFile f(first);
        QVERIFY(f.open(QIODevice::WriteOnly));
    }
    {
        QFile f(second);
        QVERIFY(f.open(QIODevice::WriteOnly));
    }

    settings::File file(dir.filePath(QStringLiteral("recent.ini")));
    file.load();
    file.updateRecentFiles(first);
    file.updateRecentFiles(second);
    file.updateRecentFiles(dir.filePath(QStringLiteral("gone.beex")));

    QCOMPARE(file.recentFiles().size(), 3);
    const QStringList existing = file.recentFiles(true);
    QCOMPARE(existing.size(), 2);
    QCOMPARE(existing.first(), second);
    QCOMPARE(existing.last(), first);
}

void TestSettings::portablePaths()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    settings::setSettingsDir(dir.path());
    QCOMPARE(settings::configDir(), dir.path());
    QCOMPARE(settings::iniPath(), dir.path() + QStringLiteral("/BeeXRef.ini"));
    QCOMPARE(settings::logPath(), dir.path() + QStringLiteral("/BeeXRef.log"));
    QCOMPARE(settings::cacheDir(), dir.path() + QStringLiteral("/cache"));

    settings::setSettingsDir(QString());
    QVERIFY(settings::configDir().endsWith(QStringLiteral("/BeeXRef")));
    QVERIFY(settings::iniPath().endsWith(QStringLiteral("/BeeXRef/BeeXRef.ini")));
    QVERIFY(settings::cacheDir().endsWith(QStringLiteral("/BeeXRef")));
}

QTEST_GUILESS_MAIN(TestSettings)

#include "test_settings.moc"
