#ifndef TANGPINGOS_TTY_H
#define TANGPINGOS_TTY_H

#include <stdint.h>

int tty_is_path(const char *path);
void tty_on_key(char c);
uint64_t tty_read(char *buffer, uint64_t len);
uint64_t tty_write(const char *buffer, uint64_t len);

#endif
