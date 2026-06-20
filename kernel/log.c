#include <console.h>
#include <log.h>
#include <stdarg.h>

static const char *log_prefix(enum log_level level) {
    switch (level) {
        case LOG_LEVEL_ERROR:
            return "[error] ";
        case LOG_LEVEL_WARN:
            return "[warn] ";
        case LOG_LEVEL_INFO:
            return "[info] ";
        case LOG_LEVEL_DEBUG:
            return "[debug] ";
        default:
            return "[log] ";
    }
}

void log_printf(enum log_level level, const char *fmt, ...) {
    if (level > TANGPINGOS_LOG_LEVEL) {
        return;
    }

    va_list args;
    va_start(args, fmt);

    console_write(log_prefix(level));
    console_vprintf(fmt, args);

    va_end(args);
}
