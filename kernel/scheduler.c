#include <console.h>
#include <log.h>
#include <memory.h>
#include <scheduler.h>
#include <stddef.h>
#include <stdint.h>
#include <tty.h>
#include <vfs.h>
#include <x86_64/arch.h>

#define MAX_TASKS 8
#define TASK_STACK_SIZE (16ULL * 1024ULL)
#define TASK_QUANTUM_TICKS 25
#define KERNEL_CODE_SELECTOR 0x08
#define KERNEL_DATA_SELECTOR 0x10
#define USER_CODE_SELECTOR 0x1b
#define USER_DATA_SELECTOR 0x23
#define MAX_PENDING_STACK_FREE 8
#define TASK_MAX_FDS 11
#define TASK_MAX_OPEN_FILES 8
#define TASK_FIRST_FILE_FD 3
#define TASK_PATH_MAX 128
#define TASK_NAME_MAX 32

enum task_file_type {
    TASK_FILE_NONE,
    TASK_FILE_VFS,
    TASK_FILE_TTY,
};

enum task_state {
    TASK_RUNNABLE,
    TASK_SLEEPING,
    TASK_WAITING_INPUT,
    TASK_STOPPED,
};

struct task_file {
    int in_use;
    int open_file_index;
};

struct task_open_file {
    int in_use;
    uint64_t ref_count;
    enum task_file_type type;
    char path[TASK_PATH_MAX];
    uint64_t offset;
    int append;
};

struct task {
    uint64_t pid;
    char name[TASK_NAME_MAX];
    uint8_t *stack;
    uint64_t stack_size;
    uint64_t kernel_stack_top;
    uint64_t cr3;
    struct interrupt_frame *frame;
    enum task_state state;
    uint64_t brk_start;
    uint64_t brk_current;
    uint64_t brk_limit;
    uint64_t wake_tick;
    uint64_t exit_status;
    uint64_t switches;
    int resources_released;
    struct task_file files[TASK_MAX_FDS];
    struct task_open_file open_files[TASK_MAX_OPEN_FILES];
};

static struct task tasks[MAX_TASKS];
static struct task boot_task;
static struct task *current_task;
static uint64_t task_count;
static uint64_t current_task_index;
static uint64_t quantum_ticks;
static uint64_t scheduler_tick_count;
static uint64_t next_pid = 1;
static int scheduler_ready;
static void *pending_stack_free[MAX_PENDING_STACK_FREE];
static uint64_t pending_stack_free_count;

static uint64_t align_down(uint64_t value, uint64_t align) {
    return value & ~(align - 1);
}

static uint64_t align_up(uint64_t value, uint64_t align) {
    return (value + align - 1) & ~(align - 1);
}

static void *memset_local(void *ptr, int value, uint64_t size) {
    uint8_t *bytes = ptr;
    for (uint64_t i = 0; i < size; i++) {
        bytes[i] = (uint8_t)value;
    }
    return ptr;
}

static void copy_file_path(char *dst, const char *src) {
    uint64_t i = 0;

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    while (i + 1 < TASK_PATH_MAX && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void copy_task_name(char *dst, const char *src) {
    uint64_t i = 0;

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    while (i + 1 < TASK_NAME_MAX && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void reset_task_file(struct task_file *file) {
    file->in_use = 0;
    file->open_file_index = -1;
}

static void reset_task_open_file(struct task_open_file *file) {
    file->in_use = 0;
    file->ref_count = 0;
    file->type = TASK_FILE_NONE;
    file->offset = 0;
    file->append = 0;
    file->path[0] = '\0';
}

static int fd_is_valid(int fd) {
    return fd >= 0 && fd < TASK_MAX_FDS;
}

static struct task_open_file *current_open_file_for_fd(int fd) {
    if (current_task == NULL || current_task == &boot_task || !fd_is_valid(fd)) {
        return NULL;
    }

    struct task_file *file = &current_task->files[fd];
    if (!file->in_use || file->open_file_index < 0 ||
        file->open_file_index >= TASK_MAX_OPEN_FILES) {
        return NULL;
    }

    struct task_open_file *open_file = &current_task->open_files[file->open_file_index];
    return open_file->in_use ? open_file : NULL;
}

static int allocate_current_open_file(enum task_file_type type, const char *path, int append) {
    for (uint64_t i = 0; i < TASK_MAX_OPEN_FILES; i++) {
        struct task_open_file *file = &current_task->open_files[i];
        if (file->in_use) {
            continue;
        }

        file->in_use = 1;
        file->ref_count = 0;
        file->type = type;
        file->offset = 0;
        file->append = append;
        copy_file_path(file->path, path);
        return (int)i;
    }

    return -1;
}

static void close_current_fd_slot(int fd) {
    if (current_task == NULL || current_task == &boot_task || !fd_is_valid(fd)) {
        return;
    }

    struct task_file *file = &current_task->files[fd];
    if (!file->in_use || file->open_file_index < 0 ||
        file->open_file_index >= TASK_MAX_OPEN_FILES) {
        reset_task_file(file);
        return;
    }

    struct task_open_file *open_file = &current_task->open_files[file->open_file_index];
    if (open_file->in_use && open_file->ref_count > 0) {
        open_file->ref_count--;
        if (open_file->ref_count == 0) {
            reset_task_open_file(open_file);
        }
    }

    reset_task_file(file);
}

static const char *task_state_name(enum task_state state) {
    switch (state) {
        case TASK_RUNNABLE:
            return "runnable";
        case TASK_SLEEPING:
            return "sleeping";
        case TASK_WAITING_INPUT:
            return "waiting-input";
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
    log_warn("task returned; stopping\n");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void scheduler_init(void) {
    task_count = 0;
    current_task_index = 0;
    quantum_ticks = 0;
    scheduler_tick_count = 0;
    current_task = NULL;
    pending_stack_free_count = 0;
    scheduler_ready = 1;
    log_info("scheduler initialized\n");
}

int scheduler_create_task(const char *name, task_entry_t entry, void *arg) {
    if (task_count >= MAX_TASKS) {
        return -1;
    }

    struct task *task = &tasks[task_count++];
    memset_local(task, 0, sizeof(*task));

    task->pid = next_pid++;
    copy_task_name(task->name, name);
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

    log_debug("task created: pid=%u name=%s stack=%x cr3=%x\n",
              task->pid, task->name, (uint64_t)task->stack, task->cr3);
    return 0;
}

int scheduler_create_user_task(const char *name, uint64_t cr3_phys,
                               uint64_t entry, uint64_t user_stack_top,
                               uint64_t arg0, uint64_t arg1,
                               uint64_t brk_start, uint64_t brk_limit) {
    if (task_count >= MAX_TASKS) {
        return -1;
    }

    struct task *task = &tasks[task_count++];
    memset_local(task, 0, sizeof(*task));

    task->pid = next_pid++;
    copy_task_name(task->name, name);
    task->stack_size = TASK_STACK_SIZE;
    task->stack = kmalloc(TASK_STACK_SIZE);
    task->state = TASK_RUNNABLE;
    task->cr3 = cr3_phys;
    task->brk_start = brk_start;
    task->brk_current = brk_start;
    task->brk_limit = brk_limit;

    uint64_t kernel_stack_top = align_down((uint64_t)task->stack + TASK_STACK_SIZE, 16) - 8;
    struct interrupt_frame *frame =
        (struct interrupt_frame *)(kernel_stack_top - sizeof(struct interrupt_frame));
    memset_local(frame, 0, sizeof(*frame));

    frame->rip = entry;
    frame->rdi = arg0;
    frame->rsi = arg1;
    frame->cs = USER_CODE_SELECTOR;
    frame->rflags = 0x202;
    frame->rsp = user_stack_top;
    frame->ss = USER_DATA_SELECTOR;

    task->kernel_stack_top = kernel_stack_top;
    task->frame = frame;

    log_info("user task created: pid=%u name=%s user_rip=%x user_rsp=%x kernel_stack=%x cr3=%x brk=%x limit=%x\n",
             task->pid, task->name, entry, user_stack_top, (uint64_t)task->stack,
             task->cr3, task->brk_current, task->brk_limit);
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

static void wake_sleeping_tasks(void) {
    for (uint64_t i = 0; i < task_count; i++) {
        struct task *task = &tasks[i];
        if (task->state == TASK_SLEEPING && task->wake_tick <= scheduler_tick_count) {
            task->state = TASK_RUNNABLE;
            log_debug("task woke: pid=%u name=%s tick=%u\n",
                      task->pid, task->name, scheduler_tick_count);
        }
    }
}

static void ensure_boot_task(struct interrupt_frame *frame) {
    if (current_task != NULL) {
        return;
    }

    memset_local(&boot_task, 0, sizeof(boot_task));
    boot_task.pid = 0;
    copy_task_name(boot_task.name, "boot");
    boot_task.state = TASK_RUNNABLE;
    boot_task.cr3 = vmm_current_address_space();
    boot_task.kernel_stack_top = align_down((uint64_t)frame + sizeof(*frame), 16);
    boot_task.frame = frame;
    current_task = &boot_task;
    current_task_index = task_count - 1;
}

static struct interrupt_frame *switch_to_task(struct task *next, const char *reason) {
    (void)reason;

    if (next == current_task) {
        return current_task->frame;
    }

    current_task = next;
    current_task->switches++;
    switch_task_context(current_task);

    return current_task->frame;
}

static void release_task_resources(struct task *task) {
    if (task == NULL || task == &boot_task || task->resources_released) {
        return;
    }
    if (task->cr3 == 0 || task->cr3 == vmm_kernel_address_space()) {
        task->resources_released = 1;
        return;
    }

    for (uint64_t i = 0; i < TASK_MAX_FDS; i++) {
        reset_task_file(&task->files[i]);
    }
    for (uint64_t i = 0; i < TASK_MAX_OPEN_FILES; i++) {
        reset_task_open_file(&task->open_files[i]);
    }

    uint64_t before = pmm_free_pages();
    uint64_t released = vmm_destroy_user_address_space(task->cr3);
    uint64_t after = pmm_free_pages();
    task->cr3 = vmm_kernel_address_space();
    task->resources_released = 1;
    log_info("task resources released: pid=%u pages=%u free=%u->%u\n",
             task->pid, released, before, after);

    if (task->stack != NULL) {
        if (pending_stack_free_count >= MAX_PENDING_STACK_FREE) {
            kernel_panic("pending stack free queue full");
        }
        pending_stack_free[pending_stack_free_count++] = task->stack;
        task->stack = NULL;
        task->kernel_stack_top = 0;
        task->frame = NULL;
    }
}

static void reclaim_pending_stacks(void) {
    while (pending_stack_free_count > 0) {
        void *stack = pending_stack_free[--pending_stack_free_count];
        kfree(stack);
        log_info("kernel stack reclaimed: %x\n", (uint64_t)stack);
    }
}

struct interrupt_frame *scheduler_on_timer_tick(struct interrupt_frame *frame) {
    if (!scheduler_ready || task_count == 0) {
        return frame;
    }

    ensure_boot_task(frame);
    scheduler_tick_count++;
    reclaim_pending_stacks();
    wake_sleeping_tasks();

    if (current_task != NULL) {
        current_task->frame = frame;
    }

    quantum_ticks++;
    if (quantum_ticks < TASK_QUANTUM_TICKS) {
        return frame;
    }

    quantum_ticks = 0;
    struct task *next = next_runnable_task();
    return switch_to_task(next, "switch to");
}

struct interrupt_frame *scheduler_exit_current(struct interrupt_frame *frame, uint64_t status) {
    if (current_task == NULL || current_task == &boot_task) {
        log_warn("sys_exit ignored without current user task\n");
        return frame;
    }

    current_task->frame = frame;
    current_task->state = TASK_STOPPED;
    current_task->exit_status = status;
    log_info("task exited: pid=%u name=%s status=%u\n",
             current_task->pid, current_task->name, status);

    quantum_ticks = 0;
    struct task *finished = current_task;
    struct task *next = next_runnable_task();
    struct interrupt_frame *next_frame = switch_to_task(next, "exit to");
    release_task_resources(finished);
    return next_frame;
}

struct interrupt_frame *scheduler_fault_current(struct interrupt_frame *frame, uint64_t vector, uint64_t fault_addr) {
    if (current_task == NULL || current_task == &boot_task) {
        kernel_panic("user fault without current user task");
    }

    current_task->frame = frame;
    current_task->state = TASK_STOPPED;
    current_task->exit_status = 128 + vector;
    log_warn("task killed: pid=%u name=%s vector=%u fault=%x status=%u\n",
             current_task->pid, current_task->name, vector, fault_addr,
             current_task->exit_status);

    quantum_ticks = 0;
    struct task *faulted = current_task;
    struct task *next = next_runnable_task();
    struct interrupt_frame *next_frame = switch_to_task(next, "fault to");
    release_task_resources(faulted);
    return next_frame;
}

struct interrupt_frame *scheduler_yield_current(struct interrupt_frame *frame) {
    if (current_task == NULL || current_task == &boot_task) {
        return frame;
    }

    current_task->frame = frame;
    quantum_ticks = 0;
    struct task *next = next_runnable_task();
    return switch_to_task(next, "yield to");
}

struct interrupt_frame *scheduler_sleep_current(struct interrupt_frame *frame, uint64_t ticks) {
    if (ticks == 0) {
        return scheduler_yield_current(frame);
    }

    if (current_task == NULL || current_task == &boot_task) {
        return frame;
    }

    current_task->frame = frame;
    current_task->state = TASK_SLEEPING;
    current_task->wake_tick = scheduler_tick_count + ticks;
    log_debug("task sleeping: pid=%u name=%s until_tick=%u\n",
              current_task->pid, current_task->name, current_task->wake_tick);

    quantum_ticks = 0;
    struct task *next = next_runnable_task();
    return switch_to_task(next, "sleep to");
}

struct interrupt_frame *scheduler_wait_current_for_input(struct interrupt_frame *frame) {
    if (current_task == NULL || current_task == &boot_task) {
        return frame;
    }

    current_task->frame = frame;
    current_task->state = TASK_WAITING_INPUT;

    quantum_ticks = 0;
    struct task *next = next_runnable_task();
    return switch_to_task(next, "input-wait to");
}

void scheduler_wake_input_waiters(void) {
    for (uint64_t i = 0; i < task_count; i++) {
        struct task *task = &tasks[i];
        if (task->state == TASK_WAITING_INPUT) {
            task->state = TASK_RUNNABLE;
        }
    }
}

int scheduler_task_info(uint64_t index, struct scheduler_task_info *out) {
    if (out == NULL) {
        return -1;
    }
    if (index >= task_count) {
        return 0;
    }

    struct task *task = &tasks[index];
    out->pid = task->pid;
    out->state = task->state;
    out->exit_status = task->exit_status;
    out->switches = task->switches;
    copy_task_name(out->name, task->name);
    return 1;
}

uint64_t scheduler_current_pid(void) {
    if (current_task == NULL) {
        return 0;
    }

    return current_task->pid;
}

uint64_t scheduler_ticks(void) {
    return scheduler_tick_count;
}

uint64_t scheduler_current_brk(void) {
    if (current_task == NULL || current_task == &boot_task) {
        return 0;
    }

    return current_task->brk_current;
}

uint64_t scheduler_set_current_brk(uint64_t new_break) {
    if (current_task == NULL || current_task == &boot_task) {
        return 0;
    }

    if (new_break == 0) {
        return current_task->brk_current;
    }

    if (new_break < current_task->brk_start || new_break > current_task->brk_limit) {
        return current_task->brk_current;
    }

    uint64_t old_break = current_task->brk_current;
    if (new_break > old_break) {
        uint64_t map_start = align_up(old_break, PAGE_SIZE);
        uint64_t map_end = align_up(new_break, PAGE_SIZE);

        for (uint64_t page = map_start; page < map_end; page += PAGE_SIZE) {
            uint64_t phys = pmm_alloc_page();
            if (vmm_map_page_in(current_task->cr3, page, phys,
                                VMM_FLAG_WRITABLE | VMM_FLAG_USER | VMM_FLAG_NO_EXEC) != 0) {
                kernel_panic("failed to grow user heap");
            }
        }

        if (map_end > map_start) {
            log_debug("heap grew: pid=%u old=%x new=%x mapped_to=%x\n",
                      current_task->pid, old_break, new_break, map_end);
        }
    }

    current_task->brk_current = new_break;
    return current_task->brk_current;
}

int scheduler_open_current_file(const char *path, uint64_t flags) {
    if (current_task == NULL || current_task == &boot_task || path == NULL) {
        return -1;
    }

    enum task_file_type type = tty_is_path(path) ? TASK_FILE_TTY : TASK_FILE_VFS;
    int append = (flags & OPEN_APPEND) != 0;
    if (type == TASK_FILE_TTY) {
        flags = 0;
        append = 0;
    }

    if (!vfs_file_exists(path)) {
        if ((flags & OPEN_CREATE) == 0 || vfs_create_file(path) != 0) {
            return -1;
        }
    } else if ((flags & OPEN_TRUNC) != 0 && vfs_truncate_file(path, 0) != 0) {
        return -1;
    }

    int open_file_index = allocate_current_open_file(type, path, append);
    if (open_file_index < 0) {
        return -1;
    }

    if (type == TASK_FILE_VFS && append) {
        uint64_t size = vfs_file_size(path);
        if (size == (uint64_t)-1) {
            reset_task_open_file(&current_task->open_files[open_file_index]);
            return -1;
        }
        current_task->open_files[open_file_index].offset = size;
    }

    for (uint64_t i = TASK_FIRST_FILE_FD; i < TASK_MAX_FDS; i++) {
        struct task_file *file = &current_task->files[i];
        if (file->in_use) {
            continue;
        }

        file->in_use = 1;
        file->open_file_index = open_file_index;
        current_task->open_files[open_file_index].ref_count++;

        int fd = (int)i;
        log_debug("fd opened: pid=%u fd=%u path=%s flags=%u\n",
                  current_task->pid, (uint64_t)fd,
                  current_task->open_files[open_file_index].path, flags);
        return fd;
    }

    reset_task_open_file(&current_task->open_files[open_file_index]);
    return -1;
}

int scheduler_current_fd_is_tty(int fd) {
    if (current_task == NULL || current_task == &boot_task) {
        return 0;
    }

    if (fd < 0 || fd >= TASK_MAX_FDS) {
        return 0;
    }

    if (!current_task->files[fd].in_use && fd >= 0 && fd <= 2) {
        return 1;
    }

    struct task_open_file *open_file = current_open_file_for_fd(fd);
    return open_file != NULL && open_file->type == TASK_FILE_TTY;
}

uint64_t scheduler_read_current_file(int fd, void *buffer, uint64_t len) {
    if (current_task == NULL || current_task == &boot_task || buffer == NULL) {
        return (uint64_t)-1;
    }

    if (fd < 0 || fd >= TASK_MAX_FDS) {
        return (uint64_t)-1;
    }

    struct task_open_file *file = current_open_file_for_fd(fd);
    if (file == NULL || file->type != TASK_FILE_VFS) {
        return (uint64_t)-1;
    }

    uint64_t read = vfs_read_file(file->path, file->offset, buffer, len);
    if (read != (uint64_t)-1) {
        file->offset += read;
    }

    return read;
}

uint64_t scheduler_write_current_file(int fd, const void *buffer, uint64_t len) {
    if (current_task == NULL || current_task == &boot_task || buffer == NULL) {
        return (uint64_t)-1;
    }

    if (fd < 0 || fd >= TASK_MAX_FDS) {
        return (uint64_t)-1;
    }

    struct task_open_file *file = current_open_file_for_fd(fd);
    if (file == NULL || file->type != TASK_FILE_VFS) {
        return (uint64_t)-1;
    }

    uint64_t write_offset = file->offset;
    if (file->append) {
        write_offset = vfs_file_size(file->path);
        if (write_offset == (uint64_t)-1) {
            return (uint64_t)-1;
        }
    }

    uint64_t written = vfs_write_existing_file(file->path, write_offset, buffer, len);
    if (written != (uint64_t)-1) {
        file->offset = write_offset + written;
    }

    return written;
}

uint64_t scheduler_seek_current_file(int fd, int64_t offset, uint64_t whence) {
    if (current_task == NULL || current_task == &boot_task) {
        return (uint64_t)-1;
    }

    if (fd < 0 || fd >= TASK_MAX_FDS) {
        return (uint64_t)-1;
    }

    struct task_open_file *file = current_open_file_for_fd(fd);
    if (file == NULL || file->type != TASK_FILE_VFS) {
        return (uint64_t)-1;
    }

    uint64_t base;
    if (whence == SEEK_SET) {
        base = 0;
    } else if (whence == SEEK_CUR) {
        base = file->offset;
    } else if (whence == SEEK_END) {
        base = vfs_file_size(file->path);
        if (base == (uint64_t)-1) {
            return (uint64_t)-1;
        }
    } else {
        return (uint64_t)-1;
    }

    uint64_t new_offset;
    if (offset < 0) {
        if (offset == INT64_MIN) {
            return (uint64_t)-1;
        }
        uint64_t delta = (uint64_t)(-offset);
        if (delta > base) {
            return (uint64_t)-1;
        }
        new_offset = base - delta;
    } else {
        uint64_t delta = (uint64_t)offset;
        if (UINT64_MAX - base < delta) {
            return (uint64_t)-1;
        }
        new_offset = base + delta;
    }

    file->offset = new_offset;
    return new_offset;
}

int scheduler_close_current_file(int fd) {
    if (current_task == NULL || current_task == &boot_task) {
        return -1;
    }

    if (fd < 0 || fd >= TASK_MAX_FDS) {
        return -1;
    }

    struct task_open_file *open_file = current_open_file_for_fd(fd);
    if (open_file == NULL) {
        return -1;
    }

    log_debug("fd closed: pid=%u fd=%u path=%s\n",
              current_task->pid, (uint64_t)fd, open_file->path);
    close_current_fd_slot(fd);
    return 0;
}

int scheduler_dup2_current_file(int old_fd, int new_fd) {
    if (current_task == NULL || current_task == &boot_task) {
        return -1;
    }
    if (!fd_is_valid(old_fd) || !fd_is_valid(new_fd)) {
        return -1;
    }

    if (old_fd == new_fd) {
        return scheduler_current_fd_is_tty(old_fd) || current_open_file_for_fd(old_fd) != NULL ? new_fd : -1;
    }

    int source_index;
    struct task_open_file *source = current_open_file_for_fd(old_fd);
    if (source == NULL && old_fd >= 0 && old_fd <= 2) {
        source_index = allocate_current_open_file(TASK_FILE_TTY, "/dev/tty", 0);
        if (source_index < 0) {
            return -1;
        }
        source = &current_task->open_files[source_index];
    } else if (source != NULL) {
        source_index = current_task->files[old_fd].open_file_index;
    } else {
        return -1;
    }

    close_current_fd_slot(new_fd);
    current_task->files[new_fd].in_use = 1;
    current_task->files[new_fd].open_file_index = source_index;
    source->ref_count++;
    log_debug("fd duplicated: pid=%u old=%u new=%u path=%s\n",
              current_task->pid, (uint64_t)old_fd, (uint64_t)new_fd,
              source->path);
    return new_fd;
}

void scheduler_dump_tasks(void) {
    log_debug("task table:\n");
    for (uint64_t i = 0; i < task_count; i++) {
        struct task *task = &tasks[i];
        log_debug("  pid=%u name=%s state=%s cr3=%x brk=%x wake=%u\n",
                  task->pid, task->name, task_state_name(task->state),
                  task->cr3, task->brk_current, task->wake_tick);
    }
}
