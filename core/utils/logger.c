#include "logger.h"

#include <stdarg.h>
#include <time.h>
#include <string.h>

/* Global (internal) settings */
static log_level_t g_log_level = LOG_LEVEL_DEBUG;
static FILE *g_log_fp = NULL;
static int g_use_colors = 1;

static const char *level_names[] = {
    "DEBUG",
    "INFO ",
    "WARN ",
    "ERROR",
    "OK   "};

static const char *level_colors[] = {
    LOG_COLOR_DEBUG,
    LOG_COLOR_INFO,
    LOG_COLOR_WARN,
    LOG_COLOR_ERROR,
    LOG_COLOR_OK};

void log_set_level(log_level_t level)
{
    g_log_level = level;
}

void log_set_fp(FILE *fp)
{
    g_log_fp = fp;
}

void log_enable_colors(int enable)
{
    g_use_colors = enable;
}

static void current_time_str(char *buf, size_t sz)
{
    time_t t = time(NULL);
    struct tm tm_buf;
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    strftime(buf, sz, "%H:%M:%S", &tm_buf);
}

void log_log(log_level_t level,
             const char *file,
             int line,
             const char *fmt, ...)
{
    if (level < g_log_level || level >= LOG_LEVEL_NONE)
    {
        return;
    }

    if (!g_log_fp)
    {
        g_log_fp = stderr;
    }

    char timebuf[16];
    current_time_str(timebuf, sizeof(timebuf));

    const char *basename = strrchr(file, '/');
#ifdef _WIN32
    const char *basename2 = strrchr(file, '\\');
    if (!basename || (basename2 && basename2 > basename))
    {
        basename = basename2;
    }
#endif
    if (basename)
    {
        basename++;
    }
    else
    {
        basename = file;
    }

    va_list args;
    va_start(args, fmt);

    if (g_use_colors)
    {
        fprintf(g_log_fp,
                LOG_COLOR_WHITE "[" LOG_COLOR_BRIGHT_BLACK "%s" LOG_COLOR_WHITE "] "

                                "["
                                "%s%s" LOG_COLOR_WHITE "] "

                LOG_COLOR_BRIGHT_BLACK "%s:%d" LOG_COLOR_WHITE ": " LOG_COLOR_RESET,
                timebuf,
                level_colors[level], level_names[level],
                basename, line);

        vfprintf(g_log_fp, fmt, args);
        fprintf(g_log_fp, LOG_COLOR_RESET "\n");
    }
    else
    {
        fprintf(g_log_fp, "[%s] [%s] %s:%d: ",
                timebuf,
                level_names[level],
                basename,
                line);
        vfprintf(g_log_fp, fmt, args);
        fprintf(g_log_fp, "\n");
    }

    va_end(args);
    fflush(g_log_fp);
}
