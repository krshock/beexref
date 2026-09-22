#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>

#include "board/board.h"
#include "constants.h"
#include "logging.h"
#include "settings.h"
#include "ui/main_window.h"

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

} // namespace

int main(int argc, char *argv[])
{
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

    parser.process(app);

    settings::setSettingsDir(parser.value(settingsDirOption));
    logging::setup(levelFromName(parser.value(logLevelOption)), settings::logPath());
    logging::info(QStringLiteral("Starting"),
                  {{QStringLiteral("name"), QString::fromLatin1(constants::AppName)},
                   {QStringLiteral("version"), QString::fromLatin1(constants::Version)}});
    board::sweepStaleTempFiles(settings::cacheDir());

    ui::MainWindow window;
    window.show();

    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty())
        window.openBoard(files.first());

    return app.exec();
}
