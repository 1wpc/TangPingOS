#ifndef VOIDOS_CONSOLE_H
#define VOIDOS_CONSOLE_H

#include <stdint.h>

void console_write(const char *s);
void console_write_u64(uint64_t value);
void console_write_hex(uint64_t value);
void console_printf(const char *fmt, ...);
void kernel_panic(const char *message);
void kernel_halt(void);

#endif
