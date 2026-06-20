#include <console.h>
#include <log.h>
#include <scheduler.h>
#include <stdint.h>
#include <sysinfo.h>
#include <syscall.h>
#include <tty.h>
#include <usercopy.h>
#include <user.h>
#include <vfs.h>

#define SYSCALL_WRITE 1
#define SYSCALL_EXIT  2
#define SYSCALL_GETPID 3
#define SYSCALL_YIELD  4
#define SYSCALL_SLEEP_TICKS 5
#define SYSCALL_BRK 6
#define SYSCALL_READ_FILE 7
#define SYSCALL_OPEN 8
#define SYSCALL_READ 9
#define SYSCALL_CLOSE 10
#define SYSCALL_GETDENTS 11
#define SYSCALL_WRITE_FD 12
#define SYSCALL_DUP2 13
#define SYSCALL_WRITE_FILE 14
#define SYSCALL_LSEEK 15
#define SYSCALL_UNLINK 16
#define SYSCALL_SPAWN 17
#define SYSCALL_TASK_INFO 18
#define SYSCALL_MEMINFO 19
#define SYSCALL_SYSINFO 20
#define SYSCALL_UPTIME 21
#define SYSCALL_WRITE_CHUNK_SIZE 128
#define SYSCALL_PATH_MAX 128
#define SYSCALL_READ_CHUNK_SIZE 512

static uint64_t min_u64(uint64_t a, uint64_t b) {
    return a < b ? a : b;
}

static uint64_t syscall_write_buffer(const char *buf, uint64_t len) {
    char chunk[SYSCALL_WRITE_CHUNK_SIZE];
    uint64_t written = 0;

    if (usercopy_validate_range(buf, len, 0) != 0) {
        return (uint64_t)-1;
    }

    while (written < len) {
        uint64_t to_copy = min_u64(sizeof(chunk), len - written);
        if (copy_from_user(chunk, buf + written, to_copy) != 0) {
            return (uint64_t)-1;
        }

        tty_write(chunk, to_copy);

        written += to_copy;
    }

    return len;
}

static uint64_t syscall_write(uint64_t fd, const char *buf, uint64_t len) {
    char chunk[SYSCALL_WRITE_CHUNK_SIZE];
    uint64_t written = 0;

    if (scheduler_current_fd_is_tty((int)fd)) {
        return syscall_write_buffer(buf, len);
    }
    if (usercopy_validate_range(buf, len, 0) != 0) {
        return (uint64_t)-1;
    }

    while (written < len) {
        uint64_t to_copy = min_u64(sizeof(chunk), len - written);
        if (copy_from_user(chunk, buf + written, to_copy) != 0) {
            return (uint64_t)-1;
        }

        uint64_t result = scheduler_write_current_file((int)fd, chunk, to_copy);
        if (result == (uint64_t)-1 || result != to_copy) {
            return (uint64_t)-1;
        }

        written += result;
    }

    return written;
}

static uint64_t syscall_read_file(const char *path, uint64_t offset, void *buffer, uint64_t len) {
    char kernel_path[SYSCALL_PATH_MAX];
    uint8_t kernel_buffer[SYSCALL_READ_CHUNK_SIZE];
    uint64_t to_read = min_u64(len, sizeof(kernel_buffer));

    if (copy_string_from_user(kernel_path, path, sizeof(kernel_path)) != 0) {
        return (uint64_t)-1;
    }
    if (usercopy_validate_range(buffer, to_read, 1) != 0) {
        return (uint64_t)-1;
    }

    uint64_t read = vfs_read_file(kernel_path, offset, kernel_buffer, to_read);
    if (read == (uint64_t)-1) {
        return read;
    }

    if (copy_to_user(buffer, kernel_buffer, read) != 0) {
        return (uint64_t)-1;
    }

    return read;
}

static uint64_t syscall_open(const char *path, uint64_t flags) {
    char kernel_path[SYSCALL_PATH_MAX];

    if (copy_string_from_user(kernel_path, path, sizeof(kernel_path)) != 0) {
        return (uint64_t)-1;
    }

    return (uint64_t)scheduler_open_current_file(kernel_path, flags);
}

static uint64_t syscall_read(uint64_t fd, void *buffer, uint64_t len, int *would_block) {
    uint8_t kernel_buffer[SYSCALL_READ_CHUNK_SIZE];
    uint64_t to_read = min_u64(len, sizeof(kernel_buffer));

    *would_block = 0;

    if (usercopy_validate_range(buffer, to_read, 1) != 0) {
        return (uint64_t)-1;
    }

    uint64_t read;
    if (scheduler_current_fd_is_tty((int)fd)) {
        read = tty_read((char *)kernel_buffer, to_read);
        if (read == 0 && to_read > 0) {
            *would_block = 1;
            return 0;
        }
    } else {
        read = scheduler_read_current_file((int)fd, kernel_buffer, to_read);
    }
    if (read == (uint64_t)-1) {
        return read;
    }

    if (copy_to_user(buffer, kernel_buffer, read) != 0) {
        return (uint64_t)-1;
    }

    return read;
}

static uint64_t syscall_close(uint64_t fd) {
    return scheduler_close_current_file((int)fd) == 0 ? 0 : (uint64_t)-1;
}

static uint64_t syscall_getdents(const char *path, uint64_t index, void *buffer, uint64_t len) {
    char kernel_path[SYSCALL_PATH_MAX];
    struct vfs_dirent dirent;

    if (len < sizeof(dirent)) {
        return (uint64_t)-1;
    }
    if (copy_string_from_user(kernel_path, path, sizeof(kernel_path)) != 0) {
        return (uint64_t)-1;
    }
    if (usercopy_validate_range(buffer, sizeof(dirent), 1) != 0) {
        return (uint64_t)-1;
    }

    int result = vfs_list_dir(kernel_path, index, &dirent);
    if (result <= 0) {
        return result == 0 ? 0 : (uint64_t)-1;
    }

    if (copy_to_user(buffer, &dirent, sizeof(dirent)) != 0) {
        return (uint64_t)-1;
    }

    return 1;
}

static uint64_t syscall_write_file(const char *path, uint64_t offset, const void *buffer, uint64_t len) {
    char kernel_path[SYSCALL_PATH_MAX];
    uint8_t kernel_buffer[SYSCALL_READ_CHUNK_SIZE];
    uint64_t written = 0;

    if (copy_string_from_user(kernel_path, path, sizeof(kernel_path)) != 0) {
        return (uint64_t)-1;
    }
    if (usercopy_validate_range(buffer, len, 0) != 0) {
        return (uint64_t)-1;
    }

    while (written < len) {
        uint64_t to_copy = min_u64(sizeof(kernel_buffer), len - written);
        if (copy_from_user(kernel_buffer, (const uint8_t *)buffer + written, to_copy) != 0) {
            return (uint64_t)-1;
        }

        uint64_t result = vfs_write_file(kernel_path, offset + written, kernel_buffer, to_copy);
        if (result == (uint64_t)-1 || result != to_copy) {
            return (uint64_t)-1;
        }

        written += result;
    }

    return written;
}

static uint64_t syscall_lseek(uint64_t fd, uint64_t offset, uint64_t whence) {
    return scheduler_seek_current_file((int)fd, (int64_t)offset, whence);
}

static uint64_t syscall_unlink(const char *path) {
    char kernel_path[SYSCALL_PATH_MAX];

    if (copy_string_from_user(kernel_path, path, sizeof(kernel_path)) != 0) {
        return (uint64_t)-1;
    }

    return vfs_unlink_file(kernel_path) == 0 ? 0 : (uint64_t)-1;
}

static uint64_t syscall_spawn(const char *path, const char *args) {
    char kernel_path[SYSCALL_PATH_MAX];
    char kernel_args[SYSCALL_PATH_MAX];

    if (copy_string_from_user(kernel_path, path, sizeof(kernel_path)) != 0) {
        return (uint64_t)-1;
    }

    if (args == 0) {
        kernel_args[0] = '\0';
    } else if (copy_string_from_user(kernel_args, args, sizeof(kernel_args)) != 0) {
        return (uint64_t)-1;
    }

    return user_spawn_from_vfs_with_args(kernel_path, kernel_args) == 0 ? 0 : (uint64_t)-1;
}

static uint64_t syscall_task_info(uint64_t index, void *buffer, uint64_t len) {
    struct scheduler_task_info info;

    if (len < sizeof(info) || usercopy_validate_range(buffer, sizeof(info), 1) != 0) {
        return (uint64_t)-1;
    }

    int result = scheduler_task_info(index, &info);
    if (result <= 0) {
        return result == 0 ? 0 : (uint64_t)-1;
    }
    if (copy_to_user(buffer, &info, sizeof(info)) != 0) {
        return (uint64_t)-1;
    }
    return 1;
}

static uint64_t syscall_meminfo(void *buffer, uint64_t len) {
    struct sys_meminfo info;

    if (len < sizeof(info) || usercopy_validate_range(buffer, sizeof(info), 1) != 0) {
        return (uint64_t)-1;
    }

    sysinfo_get_meminfo(&info);
    return copy_to_user(buffer, &info, sizeof(info)) == 0 ? 0 : (uint64_t)-1;
}

static uint64_t syscall_sysinfo(void *buffer, uint64_t len) {
    struct sys_system_info info;

    if (len < sizeof(info) || usercopy_validate_range(buffer, sizeof(info), 1) != 0) {
        return (uint64_t)-1;
    }

    sysinfo_get_system_info(&info);
    return copy_to_user(buffer, &info, sizeof(info)) == 0 ? 0 : (uint64_t)-1;
}

struct interrupt_frame *syscall_dispatch(struct interrupt_frame *frame) {
    static int reported_user_entry;

    if (!reported_user_entry) {
        log_debug("syscall dispatch: cs=%x\n", frame->cs);
        reported_user_entry = 1;
    }

    switch (frame->rax) {
        case SYSCALL_WRITE:
            frame->rax = syscall_write(frame->rdi, (const char *)frame->rsi, frame->rdx);
            break;
        case SYSCALL_EXIT:
            return scheduler_exit_current(frame, frame->rdi);
        case SYSCALL_GETPID:
            frame->rax = scheduler_current_pid();
            break;
        case SYSCALL_YIELD:
            frame->rax = 0;
            return scheduler_yield_current(frame);
        case SYSCALL_SLEEP_TICKS:
            frame->rax = 0;
            return scheduler_sleep_current(frame, frame->rdi);
        case SYSCALL_BRK:
            frame->rax = scheduler_set_current_brk(frame->rdi);
            break;
        case SYSCALL_READ_FILE:
            frame->rax = syscall_read_file(
                (const char *)frame->rdi,
                frame->rsi,
                (void *)frame->rdx,
                frame->rcx
            );
            break;
        case SYSCALL_OPEN:
            frame->rax = syscall_open((const char *)frame->rdi, frame->rsi);
            break;
        case SYSCALL_READ:
            {
                int would_block = 0;
                uint64_t result = syscall_read(frame->rdi, (void *)frame->rsi, frame->rdx, &would_block);
                if (would_block) {
                    frame->rax = SYSCALL_READ;
                    frame->rip -= 2;
                    return scheduler_wait_current_for_input(frame);
                }
                frame->rax = result;
            }
            break;
        case SYSCALL_CLOSE:
            frame->rax = syscall_close(frame->rdi);
            break;
        case SYSCALL_GETDENTS:
            frame->rax = syscall_getdents(
                (const char *)frame->rdi,
                frame->rsi,
                (void *)frame->rdx,
                frame->rcx
            );
            break;
        case SYSCALL_WRITE_FD:
            frame->rax = syscall_write(frame->rdi, (const char *)frame->rsi, frame->rdx);
            break;
        case SYSCALL_DUP2:
            frame->rax = scheduler_dup2_current_file((int)frame->rdi, (int)frame->rsi);
            break;
        case SYSCALL_WRITE_FILE:
            frame->rax = syscall_write_file(
                (const char *)frame->rdi,
                frame->rsi,
                (const void *)frame->rdx,
                frame->rcx
            );
            break;
        case SYSCALL_LSEEK:
            frame->rax = syscall_lseek(frame->rdi, frame->rsi, frame->rdx);
            break;
        case SYSCALL_UNLINK:
            frame->rax = syscall_unlink((const char *)frame->rdi);
            break;
        case SYSCALL_SPAWN:
            frame->rax = syscall_spawn((const char *)frame->rdi, (const char *)frame->rsi);
            break;
        case SYSCALL_TASK_INFO:
            frame->rax = syscall_task_info(frame->rdi, (void *)frame->rsi, frame->rdx);
            break;
        case SYSCALL_MEMINFO:
            frame->rax = syscall_meminfo((void *)frame->rdi, frame->rsi);
            break;
        case SYSCALL_SYSINFO:
            frame->rax = syscall_sysinfo((void *)frame->rdi, frame->rsi);
            break;
        case SYSCALL_UPTIME:
            frame->rax = scheduler_ticks();
            break;
        default:
            log_warn("unknown syscall: %u\n", frame->rax);
            frame->rax = (uint64_t)-1;
            break;
    }

    return frame;
}
