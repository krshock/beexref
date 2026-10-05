#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace ui {

// What launching a second BeeXRef instance needs: the program to run,
// its arguments and the environment the child starts with.
struct NewInstanceSpec
{
    QString program;
    QStringList args;
    QProcessEnvironment environment;
};

// Builds the launch of a new window. The AppImage is the special case:
// the running binary lives inside a temporary mount owned by this
// process and gone once it exits, so the child must run the .AppImage
// file again and get its own mount. A macOS .app goes through
// LaunchServices for the same reason. Everything else -- a build tree,
// /usr/bin from a package, a tarball, Windows -- runs the executable
// again.
//
// The inputs come as arguments so the decision is pure and testable.
// A custom settings directory travels to the child (--settings-dir);
// when empty nothing is passed, because naming the default directory
// would move the cache next to the configuration.
NewInstanceSpec newInstanceSpec(const QProcessEnvironment &environment, const QString &exePath,
                                const QString &customSettingsDir, bool macBundle);

// Starts a new window and returns its process id, or 0 when it could
// not start. programOverride runs that executable instead of this one
// (the smoke harness points it at the real beexref, since its own file
// would run the harness again) and disables the AppImage and bundle
// inference.
qint64 launchNewInstance(qint64 *pid = nullptr, const QString &programOverride = {});

} // namespace ui
