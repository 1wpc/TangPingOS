#ifndef TANGPINGOS_LOG_H
#define TANGPINGOS_LOG_H

enum log_level {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN = 1,
    LOG_LEVEL_INFO = 2,
    LOG_LEVEL_DEBUG = 3,
};

#ifndef TANGPINGOS_LOG_LEVEL
#define TANGPINGOS_LOG_LEVEL LOG_LEVEL_INFO
#endif

void log_printf(enum log_level level, const char *fmt, ...);

#define log_error(...) log_printf(LOG_LEVEL_ERROR, __VA_ARGS__)
#define log_warn(...) log_printf(LOG_LEVEL_WARN, __VA_ARGS__)
#define log_info(...) log_printf(LOG_LEVEL_INFO, __VA_ARGS__)
#define log_debug(...) log_printf(LOG_LEVEL_DEBUG, __VA_ARGS__)

#endif
