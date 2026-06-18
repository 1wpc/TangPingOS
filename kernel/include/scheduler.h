#ifndef VOIDOS_SCHEDULER_H
#define VOIDOS_SCHEDULER_H

#include <stdint.h>
#include <x86_64/interrupt_frame.h>

typedef void (*task_entry_t)(void *arg);

void scheduler_init(void);
int scheduler_create_task(const char *name, task_entry_t entry, void *arg);
int scheduler_create_user_task(const char *name, uint64_t cr3_phys,
                               uint64_t entry, uint64_t user_stack_top);
struct interrupt_frame *scheduler_on_timer_tick(struct interrupt_frame *frame);
struct interrupt_frame *scheduler_exit_current(struct interrupt_frame *frame, uint64_t status);
void scheduler_dump_tasks(void);

#endif
