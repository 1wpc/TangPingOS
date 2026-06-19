#ifndef TANGPINGOS_SCHEDULER_H
#define TANGPINGOS_SCHEDULER_H

#include <stdint.h>
#include <x86_64/interrupt_frame.h>

typedef void (*task_entry_t)(void *arg);

void scheduler_init(void);
int scheduler_create_task(const char *name, task_entry_t entry, void *arg);
int scheduler_create_user_task(const char *name, uint64_t cr3_phys,
                               uint64_t entry, uint64_t user_stack_top,
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
int scheduler_open_current_file(const char *path);
int scheduler_current_fd_is_tty(int fd);
uint64_t scheduler_read_current_file(int fd, void *buffer, uint64_t len);
int scheduler_close_current_file(int fd);
int scheduler_dup2_current_file(int old_fd, int new_fd);
void scheduler_dump_tasks(void);

#endif
