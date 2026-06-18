#include <console.h>
#include <scheduler.h>
#include <stdint.h>
#include <syscall.h>

#define SYSCALL_WRITE 1
#define SYSCALL_EXIT  2
#define SYSCALL_GETPID 3
#define SYSCALL_YIELD  4
#define SYSCALL_SLEEP_TICKS 5
#define SYSCALL_BRK 6

static void syscall_write(const char *buf, uint64_t len) {
    for (uint64_t i = 0; i < len; i++) {
        char c[2] = {buf[i], '\0'};
        console_write(c);
    }
}

struct interrupt_frame *syscall_dispatch(struct interrupt_frame *frame) {
    static int reported_user_entry;

    if (!reported_user_entry) {
        console_printf("syscall dispatch: cs=%x\n", frame->cs);
        reported_user_entry = 1;
    }

    switch (frame->rax) {
        case SYSCALL_WRITE:
            syscall_write((const char *)frame->rdi, frame->rsi);
            frame->rax = frame->rsi;
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
        default:
            console_printf("unknown syscall: %u\n", frame->rax);
            frame->rax = (uint64_t)-1;
            break;
    }

    return frame;
}
