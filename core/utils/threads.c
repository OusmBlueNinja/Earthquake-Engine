#include "utils/threads.h"

#if defined(_WIN32)

// Target Windows 7 or higher for modern topology APIs
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <malloc.h>

/* --- Windows Implementation --- */

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
    DWORD length = 0;
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &length) &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)malloc(length);
        if (buffer)
        {
            if (GetLogicalProcessorInformationEx(RelationProcessorCore, buffer, &length))
            {
                uint32_t logical_count = 0;
                unsigned char *ptr = (unsigned char *)buffer;
                while (ptr < (unsigned char *)buffer + length)
                {
                    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX item = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)ptr;
                    if (item->Relationship == RelationProcessorCore)
                    {
                        // Each core can have multiple logical processors (Hyper-threading)
                        for (int i = 0; i < item->Processor.GroupCount; ++i)
                        {
                            KAFFINITY mask = item->Processor.GroupMask[i].Mask;
                            // Count set bits (logical processors) in the mask
                            while (mask)
                            {
                                mask &= (mask - 1);
                                logical_count++;
                            }
                        }
                    }
                    ptr += item->Size;
                }
                free(buffer);
                if (logical_count > 0)
                    return logical_count;
            }
            else
            {
                free(buffer);
            }
        }
    }

    DWORD n = GetActiveProcessorCount(0xFFFF);
    if (n > 0 && n != (DWORD)-1)
        return (uint32_t)n;

    SYSTEM_INFO sysinfo;
    GetNativeSystemInfo(&sysinfo);
    if (sysinfo.dwNumberOfProcessors > 0)
        return (uint32_t)sysinfo.dwNumberOfProcessors;

    return 1u;
}

#else // !_WIN32

/* --- Linux / Unix Implementation --- */

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
    return 0;
#endif
}

uint32_t threads_get_system_count(void)
{
#if defined(__linux__)
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f)
        return 0;

    int running = 0, total = 0;
    int ok = fscanf(f, "%*f %*f %*f %d/%d", &running, &total);
    fclose(f);

    return (ok == 2 && total >= 0) ? (uint32_t)total : 0;
#else
    return 0;
#endif
}

uint32_t threads_get_cpu_logical_count(void)
{
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0)
    {
        int c = CPU_COUNT(&set);
        if (c > 0)
            return (uint32_t)c;
    }
#endif

    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (uint32_t)n : 1u;
}

#endif