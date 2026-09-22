#include "process.h"

#include <cerrno>
#include <csignal>

#if defined(Q_OS_UNIX)
#include <unistd.h>
#endif

namespace util {

bool isProcessAlive(qint64 pid)
{
#if defined(Q_OS_WIN)
    Q_UNUSED(pid);
    return true;
#else
    if (pid <= 0)
        return false;
    if (::kill(static_cast<pid_t>(pid), 0) == 0)
        return true;
    return errno == EPERM;
#endif
}

} // namespace util
