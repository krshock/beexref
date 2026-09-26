#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QRunnable>
#include <QThreadPool>

#include "board/board.h"
#include "cache/session_cache.h"
#include "constants.h"
#include "logging.h"
#include "settings.h"
#include "ui/main_window.h"
#include "util/memory.h"

namespace {

logging::Level levelFromName(const QString &name)
{
    const QString upper = name.toUpper();
    if (upper == QLatin1String("TRACE"))
        return logging::Level::Trace;
    if (upper == QLatin1String("DEBUG"))
        return logging::Level::Debug;
    if (upper == QLatin1String("WARNING") || upper == QLatin1String("WARN"))
        return logging::Level::Warn;
    if (upper == QLatin1String("ERROR") || upper == QLatin1String("CRITICAL"))
        return logging::Level::Error;
    return logging::Level::Info;
}

// Stale files left by crashed runs are swept in the background, after
// the window is up: the sweep only touches other instances' files, and
// a slow disk must never delay startup.
class SweepTask : public QRunnable
{
public:
    void run() override
    {
        QElapsedTimer timer;
        timer.start();
        board::sweepStaleTempFiles(settings::cacheDir());
        cache::SessionCache::sweepStale(settings::cacheDir());
        logging::debug(QStringLiteral("Startup"),
                       {{QStringLiteral("phase"), QStringLiteral("sweep")},
                        {QStringLiteral("ms"), timer.elapsed()}});
    }
};

} // namespace

int main(int argc, char *argv[])
{
    util::configureAllocator();

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QString::fromLatin1(constants::AppName));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(constants::Version));
    QCoreApplication::setOrganizationName(QString::fromLatin1(constants::AppName));

    QCommandLineParser parser;
    parser.setApplicationDescription(QString::fromLatin1(constants::AppNameFull));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("filenames"),
                                 QStringLiteral("Board file or images to open."));

    QCommandLineOption settingsDirOption(
        QStringLiteral("settings-dir"),
        QStringLiteral("Settings directory to use instead of the default location."),
        QStringLiteral("dir"));
    parser.addOption(settingsDirOption);

    QCommandLineOption logLevelOption(
        {QStringLiteral("l"), QStringLiteral("loglevel")},
        QStringLiteral("Log level for console output (TRACE, DEBUG, INFO, WARN, ERROR)."),
        QStringLiteral("level"), QStringLiteral("INFO"));
    parser.addOption(logLevelOption);

    QCommandLineOption memAuditOption(
        QStringLiteral("mem-audit"),
        QStringLiteral("Log a memory audit line every <seconds> seconds (0 disables)."),
        QStringLiteral("seconds"), QStringLiteral("0"));
    parser.addOption(memAuditOption);

    QCommandLineOption noCacheOption(
        QStringLiteral("no-cache"),
        QStringLiteral("Disable the undo-history and level disk cache for this session."));
    parser.addOption(noCacheOption);

    parser.process(app);

    settings::setSettingsDir(parser.value(settingsDirOption));
    logging::setup(levelFromName(parser.value(logLevelOption)), settings::logPath());
    logging::info(QStringLiteral("Starting"),
                  {{QStringLiteral("name"), QString::fromLatin1(constants::AppName)},
                   {QStringLiteral("version"), QString::fromLatin1(constants::Version)}});

    QElapsedTimer startup;
    startup.start();
    ui::MainWindow window(parser.isSet(noCacheOption));
    logging::debug(QStringLiteral("Startup"),
                   {{QStringLiteral("phase"), QStringLiteral("window")},
                    {QStringLiteral("ms"), startup.restart()}});
    window.show();
    logging::debug(QStringLiteral("Startup"),
                   {{QStringLiteral("phase"), QStringLiteral("show")},
                    {QStringLiteral("ms"), startup.restart()}});
    window.startMemoryAudit(parser.value(memAuditOption).toInt());
    QThreadPool::globalInstance()->start(new SweepTask);
    // Let the background sweep finish (and log) before the logging
    // statics are torn down at exit.
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     [] { QThreadPool::globalInstance()->waitForDone(); });

    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty())
        window.openBoard(files.first());

    return app.exec();
}
