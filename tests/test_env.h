#pragma once

// Test isolation: every suite that touches the settings, the cache or the
// log points them at a throwaway directory, so a test run can never read
// from or write to the user's real configuration directory. Call
// isolate() from initTestCase() and again from cleanup(), because some
// tests clear the override when they are done.
#include "settings.h"

#include <QTemporaryDir>

namespace testenv {

inline QTemporaryDir &dir()
{
    static QTemporaryDir scratch;
    return scratch;
}

inline void isolate()
{
    if (!dir().isValid()) {
        // Better to fail loudly than to touch the real configuration.
        qFatal("test_env: cannot create a temporary settings directory");
    }
    settings::setSettingsDir(dir().path());
}

} // namespace testenv
