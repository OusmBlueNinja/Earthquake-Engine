#ifndef LOGGER_H
#define LOGGER_H

#ifndef _DEBUG
#define _DEBUG
#endif

#include <stdio.h>

typedef enum
{
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_OK,
    LOG_LEVEL_NONE
} log_level_t;

void log_set_level(log_level_t level);
void log_set_fp(FILE *fp);
void log_enable_colors(int enable);

void log_log(log_level_t level,
             const char *file,
             int line,
             const char *fmt, ...);

#define LOG_COLOR_RESET "\x1b[0m"
#define LOG_COLOR_WHITE "\x1b[37m"
#define LOG_COLOR_BRIGHT_BLACK "\x1b[90m"

#define LOG_COLOR_DEBUG "\x1b[35m"
#define LOG_COLOR_INFO "\x1b[36m"
#define LOG_COLOR_WARN "\x1b[33m"
#define LOG_COLOR_ERROR "\x1b[31m"
#define LOG_COLOR_OK "\x1b[32m"

#ifdef _DEBUG
#define LOG_DEBUG(fmt, ...) \
    log_log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) \
    ((void)0)
#endif

#define LOG_INFO(fmt, ...) \
    log_log(LOG_LEVEL_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    log_log(LOG_LEVEL_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    log_log(LOG_LEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_OK(fmt, ...) \
    log_log(LOG_LEVEL_OK, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif /* LOGGER_H */
