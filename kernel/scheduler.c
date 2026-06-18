#include <console.h>
#include <memory.h>
#include <scheduler.h>
#include <stddef.h>
#include <stdint.h>
#include <x86_64/arch.h>

#define MAX_TASKS 8
#define TASK_STACK_SIZE (16ULL * 1024ULL)
#define TASK_QUANTUM_TICKS 25
#define KERNEL_CODE_SELECTOR 0x08
#define KERNEL_DATA_SELECTOR 0x10
#define USER_CODE_SELECTOR 0x1b
#define USER_DATA_SELECTOR 0x23

enum task_state {
    TASK_RUNNABLE,
    TASK_STOPPED,
};

struct task {
    uint64_t pid;
    const char *name;
    uint8_t *stack;
    uint64_t stack_size;
    uint64_t kernel_stack_top;
    uint64_t cr3;
    struct interrupt_frame *frame;
    enum task_state state;
    uint64_t exit_status;
    uint64_t switches;
};

static struct task tasks[MAX_TASKS];
static struct task boot_task;
static struct task *current_task;
static uint64_t task_count;
static uint64_t current_task_index;
static uint64_t quantum_ticks;
static uint64_t next_pid = 1;
static int scheduler_ready;

static uint64_t align_down(uint64_t value, uint64_t align) {
    return value & ~(align - 1);
}

static void *memset_local(void *ptr, int value, uint64_t size) {
    uint8_t *bytes = ptr;
    for (uint64_t i = 0; i < size; i++) {
        bytes[i] = (uint8_t)value;
    }
    return ptr;
}

static const char *task_state_name(enum task_state state) {
    switch (state) {
        case TASK_RUNNABLE:
            return "runnable";
        case TASK_STOPPED:
            return "stopped";
        default:
            return "unknown";
    }
}

static void switch_task_context(struct task *task) {
    x86_64_set_kernel_stack(task->kernel_stack_top);
    vmm_switch_address_space(task->cr3);
}

__attribute__((noreturn))
static void task_trampoline(task_entry_t entry, void *arg) {
    entry(arg);
    console_write("task returned; stopping\n");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void scheduler_init(void) {
    task_count = 0;
    current_task_index = 0;
    quantum_ticks = 0;
    current_task = NULL;
    scheduler_ready = 1;
    console_write("scheduler initialized\n");
}

int scheduler_create_task(const char *name, task_entry_t entry, void *arg) {
    if (task_count >= MAX_TASKS) {
        return -1;
    }

    struct task *task = &tasks[task_count++];
    memset_local(task, 0, sizeof(*task));

    task->pid = next_pid++;
    task->name = name;
    task->stack_size = TASK_STACK_SIZE;
    task->stack = kmalloc(TASK_STACK_SIZE);
    task->state = TASK_RUNNABLE;
    task->cr3 = vmm_kernel_address_space();

    uint64_t stack_top = align_down((uint64_t)task->stack + TASK_STACK_SIZE, 16) - 8;
    struct interrupt_frame *frame =
        (struct interrupt_frame *)(stack_top - sizeof(struct interrupt_frame));
    memset_local(frame, 0, sizeof(*frame));

    frame->rdi = (uint64_t)entry;
    frame->rsi = (uint64_t)arg;
    frame->rip = (uint64_t)task_trampoline;
    frame->cs = KERNEL_CODE_SELECTOR;
    frame->rflags = 0x202;
    frame->rsp = stack_top;
    frame->ss = KERNEL_DATA_SELECTOR;

    task->kernel_stack_top = stack_top;
    task->frame = frame;

    console_printf("task created: pid=%u name=%s stack=%x cr3=%x\n",
                   task->pid, name, (uint64_t)task->stack, task->cr3);
    return 0;
}

int scheduler_create_user_task(const char *name, uint64_t cr3_phys,
                               uint64_t entry, uint64_t user_stack_top) {
    if (task_count >= MAX_TASKS) {
        return -1;
    }

    struct task *task = &tasks[task_count++];
    memset_local(task, 0, sizeof(*task));

    task->pid = next_pid++;
    task->name = name;
    task->stack_size = TASK_STACK_SIZE;
    task->stack = kmalloc(TASK_STACK_SIZE);
    task->state = TASK_RUNNABLE;
    task->cr3 = cr3_phys;

    uint64_t kernel_stack_top = align_down((uint64_t)task->stack + TASK_STACK_SIZE, 16) - 8;
    struct interrupt_frame *frame =
        (struct interrupt_frame *)(kernel_stack_top - sizeof(struct interrupt_frame));
    memset_local(frame, 0, sizeof(*frame));

    frame->rip = entry;
    frame->cs = USER_CODE_SELECTOR;
    frame->rflags = 0x202;
    frame->rsp = user_stack_top;
    frame->ss = USER_DATA_SELECTOR;

    task->kernel_stack_top = kernel_stack_top;
    task->frame = frame;

    console_printf("user task created: pid=%u name=%s user_rip=%x user_rsp=%x kernel_stack=%x cr3=%x\n",
                   task->pid, name, entry, user_stack_top, (uint64_t)task->stack, task->cr3);
    return 0;
}

static struct task *next_runnable_task(void) {
    if (task_count == 0) {
        return &boot_task;
    }

    for (uint64_t i = 0; i < task_count; i++) {
        current_task_index = (current_task_index + 1) % task_count;
        if (tasks[current_task_index].state == TASK_RUNNABLE) {
            return &tasks[current_task_index];
        }
    }

    return &boot_task;
}

struct interrupt_frame *scheduler_on_timer_tick(struct interrupt_frame *frame) {
    if (!scheduler_ready || task_count == 0) {
        return frame;
    }

    if (current_task == NULL) {
        memset_local(&boot_task, 0, sizeof(boot_task));
        boot_task.pid = 0;
        boot_task.name = "boot";
        boot_task.state = TASK_RUNNABLE;
        boot_task.cr3 = vmm_current_address_space();
        boot_task.kernel_stack_top = align_down((uint64_t)frame + sizeof(*frame), 16);
        boot_task.frame = frame;
        current_task = &boot_task;
        current_task_index = task_count - 1;
    } else {
        current_task->frame = frame;
    }

    quantum_ticks++;
    if (quantum_ticks < TASK_QUANTUM_TICKS) {
        return frame;
    }

    quantum_ticks = 0;
    struct task *next = next_runnable_task();
    if (next == current_task) {
        return frame;
    }

    current_task = next;
    current_task->switches++;
    switch_task_context(current_task);

    if (current_task->switches <= 4 || (current_task->switches % 20) == 0) {
        console_printf("scheduler: switch to pid=%u name=%s cr3=%x\n",
                       current_task->pid, current_task->name, current_task->cr3);
    }

    return current_task->frame;
}

struct interrupt_frame *scheduler_exit_current(struct interrupt_frame *frame, uint64_t status) {
    if (current_task == NULL || current_task == &boot_task) {
        console_printf("sys_exit ignored without current user task\n");
        return frame;
    }

    current_task->frame = frame;
    current_task->state = TASK_STOPPED;
    current_task->exit_status = status;
    console_printf("task exited: pid=%u name=%s status=%u\n",
                   current_task->pid, current_task->name, status);

    quantum_ticks = 0;
    struct task *next = next_runnable_task();
    current_task = next;
    current_task->switches++;
    switch_task_context(current_task);

    console_printf("scheduler: switch to pid=%u name=%s cr3=%x\n",
                   current_task->pid, current_task->name, current_task->cr3);
    return current_task->frame;
}

void scheduler_dump_tasks(void) {
    console_write("task table:\n");
    for (uint64_t i = 0; i < task_count; i++) {
        struct task *task = &tasks[i];
        console_printf("  pid=%u name=%s state=%s cr3=%x\n",
                       task->pid, task->name, task_state_name(task->state), task->cr3);
    }
}
