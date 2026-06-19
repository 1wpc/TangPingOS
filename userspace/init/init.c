#include <stdint.h>

#define SYS_WRITE 1
#define SYS_EXIT  2
#define SYS_GETPID 3
#define SYS_YIELD  4
#define SYS_SLEEP_TICKS 5
#define SYS_BRK 6
#define SYS_OPEN 8
#define SYS_READ 9
#define SYS_CLOSE 10
#define SYS_GETDENTS 11

#define DIRENT_NAME_MAX 64
#define STDIN_FD 0
#define SHELL_LINE_MAX 128

struct dirent {
    char name[DIRENT_NAME_MAX];
    uint64_t type;
    uint64_t size;
};

static uint64_t syscall2(uint64_t number, uint64_t arg0, uint64_t arg1) {
    uint64_t result;
    __asm__ volatile (
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg0), "S"(arg1)
        : "rcx", "r11", "memory"
    );
    return result;
}

static uint64_t syscall3(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    uint64_t result;
    __asm__ volatile (
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg0), "S"(arg1), "d"(arg2)
        : "rcx", "r11", "memory"
    );
    return result;
}

static uint64_t syscall4(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    uint64_t result;
    __asm__ volatile (
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg0), "S"(arg1), "d"(arg2), "c"(arg3)
        : "r11", "memory"
    );
    return result;
}

static uint64_t sys_write(const char *message, uint64_t length) {
    return syscall2(SYS_WRITE, (uint64_t)message, length);
}

static uint64_t sys_getpid(void) {
    return syscall2(SYS_GETPID, 0, 0);
}

static void sys_yield(void) {
    syscall2(SYS_YIELD, 0, 0);
}

static uint64_t sys_brk(uint64_t new_break) {
    return syscall2(SYS_BRK, new_break, 0);
}

static uint64_t sys_open(const char *path) {
    return syscall2(SYS_OPEN, (uint64_t)path, 0);
}

static uint64_t sys_read(uint64_t fd, void *buffer, uint64_t length) {
    return syscall3(SYS_READ, fd, (uint64_t)buffer, length);
}

static uint64_t sys_close(uint64_t fd) {
    return syscall2(SYS_CLOSE, fd, 0);
}

static uint64_t sys_getdents(const char *path, uint64_t index, struct dirent *dirent) {
    return syscall4(SYS_GETDENTS, (uint64_t)path, index, (uint64_t)dirent, sizeof(*dirent));
}

static void *sys_sbrk(uint64_t increment) {
    uint64_t old_break = sys_brk(0);
    uint64_t new_break = sys_brk(old_break + increment);
    if (new_break != old_break + increment) {
        return (void *)0;
    }

    return (void *)old_break;
}

__attribute__((noreturn))
static void sys_exit(uint64_t status) {
    syscall2(SYS_EXIT, status, 0);
    for (;;) {
        __asm__ volatile ("pause");
    }
}

static uint64_t u64_to_decimal(uint64_t value, char *buffer) {
    char tmp[20];
    uint64_t count = 0;

    if (value == 0) {
        buffer[0] = '0';
        return 1;
    }

    while (value > 0) {
        tmp[count++] = (char)('0' + (value % 10));
        value /= 10;
    }

    for (uint64_t i = 0; i < count; i++) {
        buffer[i] = tmp[count - i - 1];
    }

    return count;
}

static uint64_t u64_to_hex(uint64_t value, char *buffer) {
    static const char digits[] = "0123456789abcdef";
    uint64_t started = 0;
    uint64_t count = 0;

    buffer[count++] = '0';
    buffer[count++] = 'x';

    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t nibble = (uint8_t)((value >> shift) & 0xf);
        if (nibble != 0 || started || shift == 0) {
            buffer[count++] = digits[nibble];
            started = 1;
        }
    }

    return count;
}

static uint64_t copy_string(char *dst, const char *src) {
    uint64_t len = 0;
    while (src[len] != '\0') {
        dst[len] = src[len];
        len++;
    }
    return len;
}

static void write_literal(const char *message, uint64_t length) {
    sys_write(message, length);
}

static uint64_t string_length(const char *s) {
    uint64_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

static void write_cstr(const char *s) {
    sys_write(s, string_length(s));
}

static void write_u64_decimal(uint64_t value) {
    char buffer[20];
    uint64_t len = u64_to_decimal(value, buffer);
    sys_write(buffer, len);
}

static void write_u64_hex(uint64_t value) {
    char buffer[18];
    uint64_t len = u64_to_hex(value, buffer);
    sys_write(buffer, len);
}

static void write_dirent_name(const struct dirent *dirent) {
    uint64_t len = 0;
    while (len < DIRENT_NAME_MAX && dirent->name[len] != '\0') {
        len++;
    }
    sys_write(dirent->name, len);
}

static void cmd_ls(const char *path) {
    static const char prefix[] = "$ ls ";
    static const char newline[] = "\n";
    static const char entry_prefix[] = "  ";
    static const char fail[] = "ls: failed\n";
    struct dirent dirent;

    write_literal(prefix, sizeof(prefix) - 1);
    write_cstr(path);
    write_literal(newline, sizeof(newline) - 1);

    for (uint64_t i = 0; i < 16; i++) {
        uint64_t result = sys_getdents(path, i, &dirent);
        if (result == 0) {
            break;
        }
        if (result == (uint64_t)-1) {
            write_literal(fail, sizeof(fail) - 1);
            sys_exit(11);
        }

        write_literal(entry_prefix, sizeof(entry_prefix) - 1);
        write_dirent_name(&dirent);
        write_literal(newline, sizeof(newline) - 1);
    }
}

static void cmd_cat(const char *path, char *buffer, uint64_t buffer_len) {
    static const char prefix[] = "$ cat ";
    static const char newline[] = "\n";
    static const char fail[] = "cat: failed\n";

    write_literal(prefix, sizeof(prefix) - 1);
    write_cstr(path);
    write_literal(newline, sizeof(newline) - 1);

    uint64_t fd = sys_open(path);
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(10);
    }

    for (;;) {
        uint64_t read_len = sys_read(fd, buffer, buffer_len);
        if (read_len == (uint64_t)-1) {
            sys_close(fd);
            write_literal(fail, sizeof(fail) - 1);
            sys_exit(10);
        }
        if (read_len == 0) {
            break;
        }
        sys_write(buffer, read_len);
        if (read_len < buffer_len) {
            break;
        }
    }

    sys_close(fd);
    write_literal(newline, sizeof(newline) - 1);
}

static void run_startup_self_test(char *heap) {
    static const char heap_message[] = "init.elf: heap buffer works\n";
    static const char usercopy_ok[] = "init.elf: bad user pointer rejected\n";
    static const char usercopy_fail[] = "init.elf: bad user pointer was accepted\n";

    uint64_t heap_len = copy_string(heap, heap_message);
    sys_write(heap, heap_len);

    uint64_t fd = sys_open("/hello.txt");
    if (fd == (uint64_t)-1) {
        sys_exit(10);
    }

    uint64_t bad_read = sys_read(fd, (void *)0xffff800000000000ULL, 1);
    if (bad_read == (uint64_t)-1) {
        write_literal(usercopy_ok, sizeof(usercopy_ok) - 1);
    } else {
        write_literal(usercopy_fail, sizeof(usercopy_fail) - 1);
        sys_exit(9);
    }

    sys_close(fd);
}

static int strings_equal(const char *a, const char *b) {
    uint64_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int starts_with(const char *s, const char *prefix) {
    uint64_t i = 0;
    while (prefix[i] != '\0') {
        if (s[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static const char *skip_spaces(const char *s) {
    while (*s == ' ') {
        s++;
    }
    return s;
}

static void read_line(char *line, uint64_t max_len) {
    uint64_t len = 0;

    for (;;) {
        char c;
        uint64_t read = sys_read(STDIN_FD, &c, 1);
        if (read == 0) {
            sys_yield();
            continue;
        }
        if (read == (uint64_t)-1) {
            line[0] = '\0';
            return;
        }

        if (c == '\r') {
            c = '\n';
        }
        if (c == '\n') {
            line[len] = '\0';
            return;
        }
        if (c == '\b') {
            if (len > 0) {
                len--;
            }
            continue;
        }
        if (len + 1 < max_len) {
            line[len++] = c;
        }
    }
}

static void run_shell_command(const char *line, char *buffer, uint64_t buffer_len) {
    static const char help[] = "commands: help, ls /, cat /hello.txt\n";
    static const char unknown[] = "unknown command\n";
    static const char usage_cat[] = "usage: cat /path\n";
    static const char usage_ls[] = "usage: ls /\n";

    line = skip_spaces(line);
    if (line[0] == '\0') {
        return;
    }
    if (strings_equal(line, "help")) {
        write_literal(help, sizeof(help) - 1);
        return;
    }
    if (strings_equal(line, "ls") || strings_equal(line, "ls /")) {
        cmd_ls("/");
        return;
    }
    if (starts_with(line, "ls ")) {
        write_literal(usage_ls, sizeof(usage_ls) - 1);
        return;
    }
    if (starts_with(line, "cat ")) {
        const char *path = skip_spaces(line + 4);
        if (path[0] == '\0') {
            write_literal(usage_cat, sizeof(usage_cat) - 1);
            return;
        }
        cmd_cat(path, buffer, buffer_len);
        return;
    }

    write_literal(unknown, sizeof(unknown) - 1);
}

__attribute__((noreturn))
static void run_interactive_shell(char *buffer, uint64_t buffer_len) {
    static const char ready[] = "init.elf: interactive shell ready\n";
    static const char prompt[] = "TangPingOS> ";
    char line[SHELL_LINE_MAX];

    write_literal(ready, sizeof(ready) - 1);
    for (;;) {
        write_literal(prompt, sizeof(prompt) - 1);
        read_line(line, sizeof(line));
        run_shell_command(line, buffer, buffer_len);
    }
}

__attribute__((noreturn))
void _start(void) {
    static const char pid_prefix[] = "init.elf: pid=";
    static const char brk_prefix[] = "init.elf: brk=";
    static const char heap_fail[] = "init.elf: sbrk failed\n";
    static const char shell_prefix[] = "init.elf: startup commands\n";
    static const char newline[] = "\n";

    write_literal(pid_prefix, sizeof(pid_prefix) - 1);
    write_u64_decimal(sys_getpid());
    write_literal(newline, sizeof(newline) - 1);

    write_literal(brk_prefix, sizeof(brk_prefix) - 1);
    write_u64_hex(sys_brk(0));
    write_literal(newline, sizeof(newline) - 1);

    char *heap = sys_sbrk(512);
    if (heap == (void *)0) {
        write_literal(heap_fail, sizeof(heap_fail) - 1);
        sys_exit(8);
    }

    run_startup_self_test(heap);
    write_literal(shell_prefix, sizeof(shell_prefix) - 1);
    cmd_ls("/");
    cmd_cat("/hello.txt", heap, 256);

#ifdef TANGPINGOS_TEST_USER_FAULT
    *(volatile uint64_t *)0 = 0x55;
#endif

    run_interactive_shell(heap, 256);
}
