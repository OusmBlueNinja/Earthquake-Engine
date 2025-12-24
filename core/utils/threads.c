#include "utils/threads.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

static uint32_t threads_count_snapshot(DWORD pid_filter, int filter_enabled)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;

    THREADENTRY32 te;
    te.dwSize = sizeof(te);

    uint32_t count = 0;
    if (Thread32First(snap, &te))
    {
        do
        {
            if (!filter_enabled || te.th32OwnerProcessID == pid_filter)
                count++;
        } while (Thread32Next(snap, &te));
    }

    CloseHandle(snap);
    return count;
}

uint32_t threads_get_process_count(void)
{
    return threads_count_snapshot(GetCurrentProcessId(), 1);
}

uint32_t threads_get_system_count(void)
{
    return threads_count_snapshot(0, 0);
}

uint32_t threads_get_cpu_logical_count(void)
{
    DWORD n = 0;

    // Preferred on modern Windows (processor groups aware)
    n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (n > 0)
        return (uint32_t)n;

    // Fallback for older targets / unusual runtime failures
    SYSTEM_INFO si;
    ZeroMemory(&si, sizeof(si));
    GetNativeSystemInfo(&si);

    if (si.dwNumberOfProcessors > 0)
        return (uint32_t)si.dwNumberOfProcessors;

    return 1u;
}

#else // !_WIN32

#include <stdint.h>
#include <unistd.h>

#if defined(__linux__)
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
#endif

uint32_t threads_get_process_count(void)
{
#if defined(__linux__)
    DIR *d = opendir("/proc/self/task");
    if (!d)
        return 0;

    uint32_t count = 0;
    struct dirent *e;
    while ((e = readdir(d)))
        if (e->d_name[0] != '.')
            count++;

    closedir(d);
    return count;
#else
    // No portable cross-unix way without platform-specific APIs.
    // Return 0 to indicate "unknown" on non-Linux.
    return 0;
#endif
}

uint32_t threads_get_system_count(void)
{
#if defined(__linux__)
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f)
        return 0;

    int running = 0;
    int total = 0;
    int ok = fscanf(f, "%*f %*f %*f %d/%d", &running, &total);
    fclose(f);

    if (ok != 2 || total < 0)
        return 0;

    return (uint32_t)total;
#else
    // Not portable without platform-specific APIs.
    return 0;
#endif
}

uint32_t threads_get_cpu_logical_count(void)
{
#if defined(__linux__)
    // Best answer under containers / cpusets / affinity:
    // counts CPUs actually available to this process.
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0)
    {
        int c = CPU_COUNT(&set);
        if (c > 0)
            return (uint32_t)c;
    }
#endif

    // Generic Unix fallback
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (uint32_t)n : 1u;
}

#endif
