#include <console.h>
#include <scheduler.h>
#include <stdint.h>
#include <syscall.h>

#define SYSCALL_WRITE 1
#define SYSCALL_EXIT  2

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
        default:
            console_printf("unknown syscall: %u\n", frame->rax);
            frame->rax = (uint64_t)-1;
            break;
    }

    return frame;
}
