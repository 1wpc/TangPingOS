#ifndef TANGPINGOS_SCHEDULER_H
#define TANGPINGOS_SCHEDULER_H

#include <stdint.h>
#include <x86_64/interrupt_frame.h>

#define OPEN_CREATE 1ULL
#define OPEN_TRUNC  2ULL
#define OPEN_APPEND 4ULL
#define SEEK_SET 0ULL
#define SEEK_CUR 1ULL
#define SEEK_END 2ULL
#define TASK_INFO_NAME_MAX 32

struct scheduler_task_info {
    uint64_t pid;
    uint64_t state;
    uint64_t exit_status;
    uint64_t switches;
    char name[TASK_INFO_NAME_MAX];
};

typedef void (*task_entry_t)(void *arg);

void scheduler_init(void);
int scheduler_create_task(const char *name, task_entry_t entry, void *arg);
int scheduler_create_user_task(const char *name, uint64_t cr3_phys,
                               uint64_t entry, uint64_t user_stack_top,
                               uint64_t arg0, uint64_t arg1,
                               uint64_t brk_start, uint64_t brk_limit);
struct interrupt_frame *scheduler_on_timer_tick(struct interrupt_frame *frame);
struct interrupt_frame *scheduler_exit_current(struct interrupt_frame *frame, uint64_t status);
struct interrupt_frame *scheduler_fault_current(struct interrupt_frame *frame, uint64_t vector, uint64_t fault_addr);
struct interrupt_frame *scheduler_yield_current(struct interrupt_frame *frame);
struct interrupt_frame *scheduler_sleep_current(struct interrupt_frame *frame, uint64_t ticks);
struct interrupt_frame *scheduler_wait_current_for_input(struct interrupt_frame *frame);
void scheduler_wake_input_waiters(void);
uint64_t scheduler_current_pid(void);
uint64_t scheduler_ticks(void);
uint64_t scheduler_current_brk(void);
uint64_t scheduler_set_current_brk(uint64_t new_break);
int scheduler_open_current_file(const char *path, uint64_t flags);
int scheduler_current_fd_is_tty(int fd);
uint64_t scheduler_read_current_file(int fd, void *buffer, uint64_t len);
uint64_t scheduler_write_current_file(int fd, const void *buffer, uint64_t len);
uint64_t scheduler_seek_current_file(int fd, int64_t offset, uint64_t whence);
int scheduler_close_current_file(int fd);
int scheduler_dup2_current_file(int old_fd, int new_fd);
int scheduler_task_info(uint64_t index, struct scheduler_task_info *out);
void scheduler_dump_tasks(void);

#endif
