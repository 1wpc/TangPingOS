#ifndef TPOS_H
#define TPOS_H

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
#define SYS_DUP2 13
#define SYS_LSEEK 15
#define SYS_UNLINK 16
#define SYS_SPAWN 17
#define SYS_TASK_INFO 18
#define SYS_MEMINFO 19
#define SYS_SYSINFO 20
#define SYS_UPTIME 21
#define SYS_BLOCK_INFO 22
#define SYS_BLOCK_READ 23
#define SYS_BLOCK_WRITE 24
#define SYS_MOUNT_INFO 25

#define STDIN_FD 0
#define STDOUT_FD 1
#define STDERR_FD 2

#define DIRENT_NAME_MAX 64
#define TASK_INFO_NAME_MAX 32
#define SYSINFO_CPU_NAME_MAX 64
#define BLOCK_DEVICE_NAME_MAX 32
#define MOUNT_NAME_MAX 32
#define MOUNT_PATH_MAX 64
#define MOUNT_SOURCE_MAX 64

#define DIRENT_TYPE_FILE 1
#define DIRENT_TYPE_DIR 2
#define DIRENT_TYPE_DEVICE 3

#define OPEN_CREATE 1ULL
#define OPEN_TRUNC  2ULL
#define OPEN_APPEND 4ULL

#define SEEK_SET 0ULL
#define SEEK_CUR 1ULL
#define SEEK_END 2ULL

struct dirent {
    char name[DIRENT_NAME_MAX];
    uint64_t type;
    uint64_t size;
};

struct task_info {
    uint64_t pid;
    uint64_t state;
    uint64_t exit_status;
    uint64_t switches;
    char name[TASK_INFO_NAME_MAX];
};

struct meminfo {
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t used_pages;
    uint64_t page_size;
};

struct system_info {
    uint64_t framebuffer_width;
    uint64_t framebuffer_height;
    uint64_t framebuffer_pitch;
    uint64_t framebuffer_bpp;
    uint64_t memmap_entries;
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t used_pages;
    uint64_t page_size;
    uint64_t ticks;
    uint64_t timer_hz;
    char cpu_name[SYSINFO_CPU_NAME_MAX];
};

struct block_device_info {
    char name[BLOCK_DEVICE_NAME_MAX];
    uint64_t block_size;
    uint64_t block_count;
    uint64_t writable;
};

struct mount_info {
    char name[MOUNT_NAME_MAX];
    char path[MOUNT_PATH_MAX];
    char source[MOUNT_SOURCE_MAX];
    uint64_t writable;
};

uint64_t tpos_syscall2(uint64_t number, uint64_t arg0, uint64_t arg1);
uint64_t tpos_syscall3(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2);
uint64_t tpos_syscall4(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3);

uint64_t tpos_write(uint64_t fd, const char *buffer, uint64_t length);
uint64_t tpos_getpid(void);
void tpos_yield(void);
void tpos_sleep_ticks(uint64_t ticks);
uint64_t tpos_brk(uint64_t new_break);
void *tpos_sbrk(uint64_t increment);
uint64_t tpos_open_flags(const char *path, uint64_t flags);
uint64_t tpos_open(const char *path);
uint64_t tpos_read(uint64_t fd, void *buffer, uint64_t length);
uint64_t tpos_close(uint64_t fd);
uint64_t tpos_getdents(const char *path, uint64_t index, struct dirent *dirent);
uint64_t tpos_dup2(uint64_t old_fd, uint64_t new_fd);
uint64_t tpos_lseek(uint64_t fd, int64_t offset, uint64_t whence);
uint64_t tpos_unlink(const char *path);
uint64_t tpos_spawn(const char *path, const char *args);
uint64_t tpos_task_info(uint64_t index, struct task_info *info);
uint64_t tpos_meminfo(struct meminfo *info);
uint64_t tpos_system_info(struct system_info *info);
uint64_t tpos_uptime(void);
uint64_t tpos_block_info(uint64_t index, struct block_device_info *info);
uint64_t tpos_block_read(uint64_t index, uint64_t lba, void *buffer, uint64_t count);
uint64_t tpos_block_write(uint64_t index, uint64_t lba, const void *buffer, uint64_t count);
uint64_t tpos_mount_info(uint64_t index, struct mount_info *info);
__attribute__((noreturn)) void tpos_exit(uint64_t status);

uint64_t tpos_strlen(const char *s);
uint64_t tpos_strcpy(char *dst, const char *src);
uint64_t tpos_u64_to_decimal(uint64_t value, char *buffer);
uint64_t tpos_u64_to_hex(uint64_t value, char *buffer);
void tpos_write_literal(const char *message, uint64_t length);
void tpos_write_cstr(const char *s);
void tpos_write_cstr_limited(const char *s, uint64_t max_len);
void tpos_write_u64_decimal(uint64_t value);
void tpos_write_u64_hex(uint64_t value);
void tpos_write_hex_byte(uint8_t value);

#endif
