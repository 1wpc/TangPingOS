#include "tpos.h"

uint64_t tpos_syscall2(uint64_t number, uint64_t arg0, uint64_t arg1) {
    uint64_t result;
    __asm__ volatile (
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg0), "S"(arg1)
        : "rcx", "r11", "memory"
    );
    return result;
}

uint64_t tpos_syscall3(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    uint64_t result;
    __asm__ volatile (
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg0), "S"(arg1), "d"(arg2)
        : "rcx", "r11", "memory"
    );
    return result;
}

uint64_t tpos_syscall4(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    uint64_t result;
    __asm__ volatile (
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg0), "S"(arg1), "d"(arg2), "c"(arg3)
        : "r11", "memory"
    );
    return result;
}

uint64_t tpos_write(uint64_t fd, const char *buffer, uint64_t length) {
    return tpos_syscall3(SYS_WRITE, fd, (uint64_t)buffer, length);
}

uint64_t tpos_getpid(void) {
    return tpos_syscall2(SYS_GETPID, 0, 0);
}

void tpos_yield(void) {
    tpos_syscall2(SYS_YIELD, 0, 0);
}

void tpos_sleep_ticks(uint64_t ticks) {
    tpos_syscall2(SYS_SLEEP_TICKS, ticks, 0);
}

uint64_t tpos_brk(uint64_t new_break) {
    return tpos_syscall2(SYS_BRK, new_break, 0);
}

void *tpos_sbrk(uint64_t increment) {
    uint64_t old_break = tpos_brk(0);
    uint64_t new_break = tpos_brk(old_break + increment);
    if (new_break != old_break + increment) {
        return (void *)0;
    }

    return (void *)old_break;
}

uint64_t tpos_open_flags(const char *path, uint64_t flags) {
    return tpos_syscall2(SYS_OPEN, (uint64_t)path, flags);
}

uint64_t tpos_open(const char *path) {
    return tpos_open_flags(path, 0);
}

uint64_t tpos_read(uint64_t fd, void *buffer, uint64_t length) {
    return tpos_syscall3(SYS_READ, fd, (uint64_t)buffer, length);
}

uint64_t tpos_close(uint64_t fd) {
    return tpos_syscall2(SYS_CLOSE, fd, 0);
}

uint64_t tpos_getdents(const char *path, uint64_t index, struct dirent *dirent) {
    return tpos_syscall4(SYS_GETDENTS, (uint64_t)path, index, (uint64_t)dirent, sizeof(*dirent));
}

uint64_t tpos_dup2(uint64_t old_fd, uint64_t new_fd) {
    return tpos_syscall2(SYS_DUP2, old_fd, new_fd);
}

uint64_t tpos_lseek(uint64_t fd, int64_t offset, uint64_t whence) {
    return tpos_syscall3(SYS_LSEEK, fd, (uint64_t)offset, whence);
}

uint64_t tpos_unlink(const char *path) {
    return tpos_syscall2(SYS_UNLINK, (uint64_t)path, 0);
}

uint64_t tpos_spawn(const char *path, const char *args) {
    return tpos_syscall3(SYS_SPAWN, (uint64_t)path, (uint64_t)args, 0);
}

uint64_t tpos_task_info(uint64_t index, struct task_info *info) {
    return tpos_syscall3(SYS_TASK_INFO, index, (uint64_t)info, sizeof(*info));
}

uint64_t tpos_meminfo(struct meminfo *info) {
    return tpos_syscall2(SYS_MEMINFO, (uint64_t)info, sizeof(*info));
}

uint64_t tpos_system_info(struct system_info *info) {
    return tpos_syscall2(SYS_SYSINFO, (uint64_t)info, sizeof(*info));
}

uint64_t tpos_uptime(void) {
    return tpos_syscall2(SYS_UPTIME, 0, 0);
}

uint64_t tpos_block_info(uint64_t index, struct block_device_info *info) {
    return tpos_syscall3(SYS_BLOCK_INFO, index, (uint64_t)info, sizeof(*info));
}

uint64_t tpos_block_read(uint64_t index, uint64_t lba, void *buffer, uint64_t count) {
    return tpos_syscall4(SYS_BLOCK_READ, index, lba, (uint64_t)buffer, count);
}

uint64_t tpos_block_write(uint64_t index, uint64_t lba, const void *buffer, uint64_t count) {
    return tpos_syscall4(SYS_BLOCK_WRITE, index, lba, (uint64_t)buffer, count);
}

uint64_t tpos_mount_info(uint64_t index, struct mount_info *info) {
    return tpos_syscall3(SYS_MOUNT_INFO, index, (uint64_t)info, sizeof(*info));
}

__attribute__((noreturn))
void tpos_exit(uint64_t status) {
    tpos_syscall2(SYS_EXIT, status, 0);
    for (;;) {
        __asm__ volatile ("pause");
    }
}
