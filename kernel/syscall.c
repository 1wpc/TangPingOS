#include <console.h>
#include <scheduler.h>
#include <stdint.h>
#include <syscall.h>
#include <usercopy.h>
#include <vfs.h>

#define SYSCALL_WRITE 1
#define SYSCALL_EXIT  2
#define SYSCALL_GETPID 3
#define SYSCALL_YIELD  4
#define SYSCALL_SLEEP_TICKS 5
#define SYSCALL_BRK 6
#define SYSCALL_READ_FILE 7
#define SYSCALL_WRITE_CHUNK_SIZE 128
#define SYSCALL_PATH_MAX 128
#define SYSCALL_READ_CHUNK_SIZE 512

static uint64_t min_u64(uint64_t a, uint64_t b) {
    return a < b ? a : b;
}

static uint64_t syscall_write(const char *buf, uint64_t len) {
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

        for (uint64_t i = 0; i < to_copy; i++) {
            char c[2] = {chunk[i], '\0'};
            console_write(c);
        }

        written += to_copy;
    }

    return len;
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

struct interrupt_frame *syscall_dispatch(struct interrupt_frame *frame) {
    static int reported_user_entry;

    if (!reported_user_entry) {
        console_printf("syscall dispatch: cs=%x\n", frame->cs);
        reported_user_entry = 1;
    }

    switch (frame->rax) {
        case SYSCALL_WRITE:
            frame->rax = syscall_write((const char *)frame->rdi, frame->rsi);
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
        default:
            console_printf("unknown syscall: %u\n", frame->rax);
            frame->rax = (uint64_t)-1;
            break;
    }

    return frame;
}
