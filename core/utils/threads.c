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
    DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return n ? (uint32_t)n : 1u;
}

#else
#include <dirent.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

uint32_t threads_get_process_count(void)
{
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
}

uint32_t threads_get_system_count(void)
{
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
}

uint32_t threads_get_cpu_logical_count(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (uint32_t)n : 1u;
}
#endif
