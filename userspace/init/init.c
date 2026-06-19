#include <stdint.h>

#define SYS_WRITE 1
#define SYS_EXIT  2
#define SYS_GETPID 3
#define SYS_YIELD  4
#define SYS_SLEEP_TICKS 5
#define SYS_BRK 6
#define SYS_READ_FILE 7

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

static void sys_sleep_ticks(uint64_t ticks) {
    syscall2(SYS_SLEEP_TICKS, ticks, 0);
}

static uint64_t sys_brk(uint64_t new_break) {
    return syscall2(SYS_BRK, new_break, 0);
}

static uint64_t sys_read_file(const char *path, uint64_t offset, void *buffer, uint64_t length) {
    return syscall4(SYS_READ_FILE, (uint64_t)path, offset, (uint64_t)buffer, length);
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

__attribute__((noreturn))
void _start(void) {
    static const char pid_prefix[] = "init.elf: pid=";
    static const char brk_prefix[] = "init.elf: brk=";
    static const char heap_message[] = "init.elf: heap buffer works\n";
    static const char heap_fail[] = "init.elf: sbrk failed\n";
    static const char initrd_prefix[] = "initrd /hello.txt: ";
    static const char initrd_fail[] = "init.elf: read_file failed\n";
    static const char usercopy_ok[] = "init.elf: bad user pointer rejected\n";
    static const char usercopy_fail[] = "init.elf: bad user pointer was accepted\n";
    static const char newline[] = "\n";
    static const char before_sleep[] = "init.elf: sleep 50 ticks\n";
    static const char after_sleep[] = "init.elf: woke and yielding\n";
    char pid_buffer[20];
    char brk_buffer[18];

    uint64_t pid_len = u64_to_decimal(sys_getpid(), pid_buffer);
    sys_write(pid_prefix, sizeof(pid_prefix) - 1);
    sys_write(pid_buffer, pid_len);
    sys_write(newline, sizeof(newline) - 1);

    uint64_t brk_len = u64_to_hex(sys_brk(0), brk_buffer);
    sys_write(brk_prefix, sizeof(brk_prefix) - 1);
    sys_write(brk_buffer, brk_len);
    sys_write(newline, sizeof(newline) - 1);

    char *heap = sys_sbrk(512);
    if (heap == (void *)0) {
        sys_write(heap_fail, sizeof(heap_fail) - 1);
        sys_exit(8);
    }

    uint64_t heap_len = copy_string(heap, heap_message);
    sys_write(heap, heap_len);

    uint64_t read_len = sys_read_file("/hello.txt", 0, heap, 256);
    if (read_len == (uint64_t)-1) {
        sys_write(initrd_fail, sizeof(initrd_fail) - 1);
    } else {
        sys_write(initrd_prefix, sizeof(initrd_prefix) - 1);
        sys_write(heap, read_len);
        if (read_len == 0 || heap[read_len - 1] != '\n') {
            sys_write(newline, sizeof(newline) - 1);
        }
    }

    uint64_t bad_read = sys_read_file("/hello.txt", 0, (void *)0xffff800000000000ULL, 1);
    if (bad_read == (uint64_t)-1) {
        sys_write(usercopy_ok, sizeof(usercopy_ok) - 1);
    } else {
        sys_write(usercopy_fail, sizeof(usercopy_fail) - 1);
        sys_exit(9);
    }

#ifdef TANGPINGOS_TEST_USER_FAULT
    *(volatile uint64_t *)0 = 0x55;
#endif

    for (uint64_t i = 0; i < 3; i++) {
        sys_write(before_sleep, sizeof(before_sleep) - 1);
        sys_sleep_ticks(50);
        sys_write(after_sleep, sizeof(after_sleep) - 1);
        sys_yield();
    }

    sys_exit(7);
}
