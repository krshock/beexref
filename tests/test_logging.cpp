#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "logging.h"

class TestLogging : public QObject
{
    Q_OBJECT

private slots:
    void writesAllLevelsToFile();
    void sessionTextHidesPreviousSessions();
    void sessionTextWithoutMarkerReturnsWholeFile();
};

void TestLogging::writesAllLevelsToFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString logfile = dir.filePath(QStringLiteral("logs/BeeXRef.log"));
    logging::setup(logging::Level::Info, logfile);
    logging::info(QStringLiteral("Starting"),
                  {{QStringLiteral("name"), QStringLiteral("BeeXRef")},
                   {QStringLiteral("version"), QStringLiteral("0.1.0")}});
    logging::debug(QStringLiteral("Hidden on console"));
    logging::trace(QStringLiteral("Traced"));

    QFile file(logfile);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QString::fromUtf8(file.readAll());

    QVERIFY(text.contains(QStringLiteral("msg=\"Log session start\"")));
    QVERIFY(text.contains(QStringLiteral("level=INFO msg=Starting name=BeeXRef version=0.1.0")));
    QVERIFY(text.contains(QStringLiteral("level=DEBUG-4 msg=\"Hidden on console\"")));
    QVERIFY(text.contains(QStringLiteral("level=INFO-8 msg=Traced")));
}

void TestLogging::sessionTextHidesPreviousSessions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString logfile = dir.filePath(QStringLiteral("BeeXRef.log"));
    logging::setup(logging::Level::Info, logfile);
    logging::info(QStringLiteral("First session"));
    QVERIFY(logging::sessionText(logfile).contains(QStringLiteral("First session")));

    logging::setup(logging::Level::Info, logfile);
    logging::info(QStringLiteral("Second session"));
    const QString text = logging::sessionText(logfile);
    QVERIFY(text.contains(QStringLiteral("Second session")));
    QVERIFY(!text.contains(QStringLiteral("First session")));
    QVERIFY(!text.contains(QStringLiteral("Log session start")));
}

void TestLogging::sessionTextWithoutMarkerReturnsWholeFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString logfile = dir.filePath(QStringLiteral("plain.log"));
    QFile file(logfile);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("level=INFO msg=Only\n");
    file.close();

    QCOMPARE(logging::sessionText(logfile), QStringLiteral("level=INFO msg=Only\n"));
    QCOMPARE(logging::sessionText(dir.filePath(QStringLiteral("missing.log"))), QString());
}

QTEST_GUILESS_MAIN(TestLogging)

#include "test_logging.moc"
