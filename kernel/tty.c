#include <console.h>
#include <input.h>
#include <scheduler.h>
#include <stdint.h>
#include <tty.h>

static int strings_equal(const char *a, const char *b) {
    if (a == 0 || b == 0) {
        return 0;
    }

    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

int tty_is_path(const char *path) {
    return strings_equal(path, "/dev/tty");
}

void tty_on_key(char c) {
    input_push_key(c);
    scheduler_wake_input_waiters();

    if (c == '\n') {
        console_write("\n");
    } else if (c == '\b') {
        console_write("\b \b");
    } else {
        char s[2] = {c, '\0'};
        console_write(s);
    }
}

uint64_t tty_read(char *buffer, uint64_t len) {
    return input_read(buffer, len);
}

uint64_t tty_write(const char *buffer, uint64_t len) {
    for (uint64_t i = 0; i < len; i++) {
        char c[2] = {buffer[i], '\0'};
        console_write(c);
    }

    return len;
}
