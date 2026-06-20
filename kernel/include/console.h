#ifndef TANGPINGOS_CONSOLE_H
#define TANGPINGOS_CONSOLE_H

#include <stdarg.h>
#include <stdint.h>

struct limine_framebuffer;

void console_init_serial(void);
void console_init_framebuffer(struct limine_framebuffer *framebuffer);
void console_set_framebuffer_enabled(int enabled);
void console_set_screen_output_suppressed(int suppressed);
void console_clear_framebuffer(void);
void console_enable_framebuffer_write_combining(void);
void console_write(const char *s);
void console_write_n(const char *s, uint64_t len);
void console_write_u64(uint64_t value);
void console_write_hex(uint64_t value);
void console_vprintf(const char *fmt, va_list args);
void console_printf(const char *fmt, ...);
void kernel_panic(const char *message);
void kernel_halt(void);

#endif
