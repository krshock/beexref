#include "new_instance.h"

#include "settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace ui {
namespace {

// The .app bundle a macOS executable lives in, or empty when it is not
// inside one (a build tree, say).
QString macBundleFor(const QString &exePath)
{
    const QDir macos = QFileInfo(exePath).absoluteDir();
    if (macos.dirName() != QLatin1String("MacOS"))
        return {};
    const QDir contents = QFileInfo(macos.absolutePath()).absoluteDir();
    if (contents.dirName() != QLatin1String("Contents"))
        return {};
    const QDir bundle = QFileInfo(contents.absolutePath()).absoluteDir();
    if (!bundle.dirName().endsWith(QLatin1String(".app")))
        return {};
    return bundle.absolutePath();
}

bool executableIsInMacBundle(const QString &exePath)
{
#ifdef Q_OS_MACOS
    return !macBundleFor(exePath).isEmpty();
#else
    Q_UNUSED(exePath);
    return false;
#endif
}

} // namespace

NewInstanceSpec newInstanceSpec(const QProcessEnvironment &environment, const QString &exePath,
                                const QString &customSettingsDir, bool macBundle)
{
    NewInstanceSpec spec;
    spec.environment = environment;

    QStringList args;
    if (!customSettingsDir.isEmpty())
        args << QStringLiteral("--settings-dir") << customSettingsDir;

    // The AppImage: run the file again, not the copy inside this
    // process's mount. A stale path (moved or deleted while running)
    // falls back to the executable, which at least works while this
    // window stays open.
    const QString appImage = environment.value(QStringLiteral("APPIMAGE"));
    if (!appImage.isEmpty() && QFileInfo(appImage).isExecutable()) {
        spec.program = appImage;
        spec.args = args;
        // The runtime sets these again for the child; a stale APPDIR
        // would only confuse it.
        for (const char *key : {"APPIMAGE", "APPDIR", "OWD", "ARGV0"})
            spec.environment.remove(QString::fromLatin1(key));
        return spec;
    }

    if (macBundle) {
        const QString bundle = macBundleFor(exePath);
        if (!bundle.isEmpty()) {
            // LaunchServices opens another instance; the app's arguments
            // follow --args.
            spec.program = QStringLiteral("/usr/bin/open");
            spec.args = QStringList({QStringLiteral("-n"), bundle});
            if (!args.isEmpty())
                spec.args << QStringLiteral("--args") << args;
            return spec;
        }
    }

    spec.program = exePath;
    spec.args = args;
    return spec;
}

qint64 launchNewInstance(qint64 *pid, const QString &programOverride)
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QString exePath = QCoreApplication::applicationFilePath();
    bool macBundle = executableIsInMacBundle(exePath);
    if (!programOverride.isEmpty()) {
        exePath = programOverride;
        macBundle = false;
        environment.remove(QStringLiteral("APPIMAGE"));
    }

    const NewInstanceSpec spec =
        newInstanceSpec(environment, exePath, settings::customSettingsDir(), macBundle);
    QProcess process;
    process.setProgram(spec.program);
    process.setArguments(spec.args);
    process.setProcessEnvironment(spec.environment);
    qint64 childPid = 0;
    if (!process.startDetached(&childPid))
        childPid = 0;
    if (pid)
        *pid = childPid;
    return childPid;
}

} // namespace ui
