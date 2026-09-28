#include "process.h"

#include <cerrno>

#if defined(Q_OS_UNIX)
#include <csignal>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace util {

bool isProcessAlive(qint64 pid)
{
#if defined(Q_OS_WIN)
    if (pid <= 0 || pid > qint64(0xFFFFFFFF))
        return false;
    // Query-limited information plus synchronization is the least a
    // process handle can be opened with; another user's live process
    // answers access denied, and that counts as alive so its files are
    // never swept. A signaled handle means the process has exited.
    HANDLE handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE,
                                  static_cast<DWORD>(pid));
    if (!handle)
        return ::GetLastError() == ERROR_ACCESS_DENIED;
    const bool alive = ::WaitForSingleObject(handle, 0) == WAIT_TIMEOUT;
    ::CloseHandle(handle);
    return alive;
#else
    if (pid <= 0)
        return false;
    if (::kill(static_cast<pid_t>(pid), 0) == 0)
        return true;
    return errno == EPERM;
#endif
}

} // namespace util
