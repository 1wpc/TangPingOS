#include "tpos.h"

__attribute__((noreturn))
void _start(void) {
    tpos_write_cstr("init.elf: pid=");
    tpos_write_u64_decimal(tpos_getpid());
    tpos_write_cstr("\n");

    uint64_t tty = tpos_open("/dev/tty");
    if (tty == (uint64_t)-1) {
        tpos_write_cstr("init.elf: failed to open /dev/tty\n");
        tpos_exit(10);
    }

    if (tpos_dup2(tty, STDIN_FD) != STDIN_FD ||
        tpos_dup2(tty, STDOUT_FD) != STDOUT_FD ||
        tpos_dup2(tty, STDERR_FD) != STDERR_FD) {
        tpos_write_cstr("init.elf: failed to bind stdio to /dev/tty\n");
        tpos_exit(11);
    }

    tpos_write_cstr("init.elf: spawning /bin/shell.elf\n");
    if (tpos_spawn("/bin/shell.elf", "") != 0) {
        tpos_write_cstr("init.elf: failed to spawn shell\n");
        tpos_exit(12);
    }

    tpos_write_cstr("init.elf: shell started\n");
    for (;;) {
        tpos_sleep_ticks(100);
        tpos_yield();
    }
}
