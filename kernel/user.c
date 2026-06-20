#include <console.h>
#include <limine.h>
#include <log.h>
#include <memory.h>
#include <scheduler.h>
#include <stdint.h>
#include <user.h>
#include <vfs.h>

#define USER_ADDRESS_MIN 0x0000000000010000ULL
#define USER_ADDRESS_MAX 0x0000800000000000ULL
#define USER_STACK_TOP   0x0000400010000000ULL
#define USER_STACK_SIZE  (16ULL * PAGE_SIZE)
#define USER_HEAP_SIZE   (16ULL * 1024ULL * 1024ULL)
#define USER_SPAWN_IMAGE_MAX (1024ULL * 1024ULL)
#define USER_TASK_NAME_MAX 32
#define USER_ARGV_MAX 8
#define USER_ARG_MAX 64

#define EI_NIDENT 16
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_EXEC 2
#define EM_X86_64 62
#define PT_LOAD 1
#define PF_X 1
#define PF_W 2

struct __attribute__((packed)) elf64_ehdr {
    uint8_t e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct __attribute__((packed)) elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

struct user_image {
    uint64_t entry;
    uint64_t brk_start;
};

static int user_range_valid(uint64_t start, uint64_t size);

static uint64_t align_up(uint64_t value, uint64_t align) {
    return (value + align - 1) & ~(align - 1);
}

static uint64_t align_down(uint64_t value, uint64_t align) {
    return value & ~(align - 1);
}

static uint64_t min_u64(uint64_t a, uint64_t b) {
    return a < b ? a : b;
}

static uint64_t max_u64(uint64_t a, uint64_t b) {
    return a > b ? a : b;
}

static void *memcpy_local(void *dst, const void *src, uint64_t len) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (uint64_t i = 0; i < len; i++) {
        d[i] = s[i];
    }
    return dst;
}

static void *memset_local(void *dst, int value, uint64_t len) {
    uint8_t *out = dst;
    for (uint64_t i = 0; i < len; i++) {
        out[i] = (uint8_t)value;
    }
    return dst;
}

static uint64_t string_length(const char *s) {
    uint64_t len = 0;
    while (s != 0 && s[len] != '\0') {
        len++;
    }
    return len;
}

static int string_equals(const char *a, const char *b) {
    if (a == 0 || b == 0) {
        return 0;
    }

    while (*a != '\0' && *b != '\0') {
        if (*a++ != *b++) {
            return 0;
        }
    }

    return *a == '\0' && *b == '\0';
}

static int string_ends_with(const char *value, const char *suffix) {
    if (value == 0 || suffix == 0) {
        return 0;
    }

    uint64_t value_len = 0;
    uint64_t suffix_len = 0;
    while (value[value_len] != '\0') {
        value_len++;
    }
    while (suffix[suffix_len] != '\0') {
        suffix_len++;
    }

    if (suffix_len > value_len) {
        return 0;
    }

    return string_equals(value + value_len - suffix_len, suffix);
}

static const char *safe_string(const char *value) {
    return value == 0 ? "(null)" : value;
}

static void copy_task_name(char *dst, const char *path) {
    const char *name = path;
    uint64_t i = 0;

    if (path == 0) {
        dst[0] = '\0';
        return;
    }

    for (uint64_t j = 0; path[j] != '\0'; j++) {
        if (path[j] == '/' && path[j + 1] != '\0') {
            name = path + j + 1;
        }
    }

    while (i + 1 < USER_TASK_NAME_MAX && name[i] != '\0') {
        dst[i] = name[i];
        i++;
    }
    dst[i] = '\0';
}

static int copy_to_user_in_space(uint64_t cr3_phys, uint64_t user_dst,
                                 const void *src, uint64_t len) {
    const uint8_t *in = (const uint8_t *)src;
    uint64_t copied = 0;

    if (len == 0) {
        return 0;
    }
    if (src == 0 || !user_range_valid(user_dst, len)) {
        return -1;
    }

    while (copied < len) {
        uint64_t addr = user_dst + copied;
        uint64_t phys;
        if (vmm_translate_user_addr(cr3_phys, addr, 1, &phys) != 0) {
            return -1;
        }

        uint64_t page_remaining = PAGE_SIZE - (addr & (PAGE_SIZE - 1));
        uint64_t to_copy = min_u64(page_remaining, len - copied);
        uint8_t *out = (uint8_t *)phys_to_virt(phys);

        for (uint64_t i = 0; i < to_copy; i++) {
            out[i] = in[copied + i];
        }
        copied += to_copy;
    }

    return 0;
}

static int copy_arg(char *dst, const char *src, uint64_t len) {
    if (len + 1 > USER_ARG_MAX) {
        return -1;
    }
    for (uint64_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
    dst[len] = '\0';
    return 0;
}

static int parse_spawn_args(const char *path, const char *args,
                            char values[USER_ARGV_MAX][USER_ARG_MAX],
                            uint64_t *argc_out) {
    uint64_t argc = 0;
    const char *p = args == 0 ? "" : args;

    if (copy_arg(values[argc++], path, string_length(path)) != 0) {
        return -1;
    }

    while (*p != '\0') {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (argc >= USER_ARGV_MAX) {
            return -1;
        }

        const char *start = p;
        uint64_t len = 0;
        while (p[len] != '\0' && p[len] != ' ') {
            len++;
        }
        if (copy_arg(values[argc++], start, len) != 0) {
            return -1;
        }
        p = start + len;
    }

    *argc_out = argc;
    return 0;
}

static int build_initial_user_stack(uint64_t cr3_phys, const char *path, const char *args,
                                    uint64_t *stack_top_out, uint64_t *argc_out,
                                    uint64_t *argv_out) {
    char values[USER_ARGV_MAX][USER_ARG_MAX];
    uint64_t argv_ptrs[USER_ARGV_MAX + 1];
    uint64_t argc = 0;
    uint64_t sp = USER_STACK_TOP;

    memset_local(values, 0, sizeof(values));
    memset_local(argv_ptrs, 0, sizeof(argv_ptrs));

    if (parse_spawn_args(path, args, values, &argc) != 0) {
        return -1;
    }

    for (uint64_t i = argc; i > 0; i--) {
        uint64_t index = i - 1;
        uint64_t len = string_length(values[index]) + 1;
        sp -= len;
        if (copy_to_user_in_space(cr3_phys, sp, values[index], len) != 0) {
            return -1;
        }
        argv_ptrs[index] = sp;
    }

    sp &= ~0xfULL;
    sp -= sizeof(argv_ptrs[0]);
    uint64_t null_ptr = 0;
    if (copy_to_user_in_space(cr3_phys, sp, &null_ptr, sizeof(null_ptr)) != 0) {
        return -1;
    }

    for (uint64_t i = argc; i > 0; i--) {
        uint64_t index = i - 1;
        sp -= sizeof(argv_ptrs[0]);
        if (copy_to_user_in_space(cr3_phys, sp, &argv_ptrs[index], sizeof(argv_ptrs[index])) != 0) {
            return -1;
        }
    }

    *stack_top_out = sp;
    *argc_out = argc;
    *argv_out = sp;
    return 0;
}

static int user_range_valid(uint64_t start, uint64_t size) {
    if (size == 0) {
        return 1;
    }

    if (start < USER_ADDRESS_MIN || start >= USER_ADDRESS_MAX) {
        return 0;
    }

    if (size > USER_ADDRESS_MAX - start) {
        return 0;
    }

    return 1;
}

static int range_overlaps_stack(uint64_t start, uint64_t end) {
    uint64_t stack_start = USER_STACK_TOP - USER_STACK_SIZE;
    return start < USER_STACK_TOP && end > stack_start;
}

static struct limine_file *find_init_module(struct limine_module_response *modules) {
    if (modules == 0) {
        kernel_panic("no module response from bootloader");
    }

    log_debug("Limine modules: %u\n", modules->module_count);

    for (uint64_t i = 0; i < modules->module_count; i++) {
        struct limine_file *module = modules->modules[i];
        log_debug("module %u: path=%s string=%s size=%u\n",
                  i, safe_string(module->path), safe_string(module->string), module->size);

        if (string_equals(module->string, "init") ||
            string_ends_with(module->path, "/init.elf")) {
            return module;
        }
    }

    kernel_panic("init.elf module not found");
    return 0;
}

static void validate_elf_header(const struct elf64_ehdr *ehdr, uint64_t image_size) {
    if (image_size < sizeof(*ehdr)) {
        kernel_panic("init module too small for ELF header");
    }

    if (ehdr->e_ident[0] != ELFMAG0 ||
        ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 ||
        ehdr->e_ident[3] != ELFMAG3) {
        kernel_panic("init module is not an ELF file");
    }

    if (ehdr->e_ident[4] != ELFCLASS64 ||
        ehdr->e_ident[5] != ELFDATA2LSB ||
        ehdr->e_ident[6] != EV_CURRENT) {
        kernel_panic("unsupported init ELF format");
    }

    if (ehdr->e_type != ET_EXEC || ehdr->e_machine != EM_X86_64 ||
        ehdr->e_version != EV_CURRENT) {
        kernel_panic("init ELF is not an x86_64 executable");
    }

    if (ehdr->e_ehsize != sizeof(*ehdr) ||
        ehdr->e_phentsize != sizeof(struct elf64_phdr) ||
        ehdr->e_phnum == 0) {
        kernel_panic("invalid init ELF program header table");
    }

    uint64_t phdr_size = (uint64_t)ehdr->e_phentsize * ehdr->e_phnum;
    if (ehdr->e_phoff > image_size || phdr_size > image_size - ehdr->e_phoff) {
        kernel_panic("init ELF program headers out of range");
    }

    if (!user_range_valid(ehdr->e_entry, 1)) {
        kernel_panic("init ELF entry is outside userspace");
    }
}

static uint64_t segment_vmm_flags(const struct elf64_phdr *phdr) {
    uint64_t flags = VMM_FLAG_USER;

    if ((phdr->p_flags & PF_W) != 0) {
        flags |= VMM_FLAG_WRITABLE;
    }

    if ((phdr->p_flags & PF_X) == 0) {
        flags |= VMM_FLAG_NO_EXEC;
    }

    return flags;
}

static void map_elf_segment(uint64_t cr3_phys, const uint8_t *image,
                            uint64_t image_size, const struct elf64_phdr *phdr) {
    if (phdr->p_memsz == 0) {
        return;
    }

    if (phdr->p_filesz > phdr->p_memsz) {
        kernel_panic("init ELF segment filesz exceeds memsz");
    }

    if (phdr->p_offset > image_size || phdr->p_filesz > image_size - phdr->p_offset) {
        kernel_panic("init ELF segment file range out of bounds");
    }

    if (!user_range_valid(phdr->p_vaddr, phdr->p_memsz)) {
        kernel_panic("init ELF segment is outside userspace");
    }

    if ((phdr->p_vaddr & (PAGE_SIZE - 1)) != (phdr->p_offset & (PAGE_SIZE - 1))) {
        kernel_panic("init ELF segment has incompatible alignment");
    }

    uint64_t mem_end = phdr->p_vaddr + phdr->p_memsz;
    uint64_t file_end = phdr->p_vaddr + phdr->p_filesz;
    uint64_t seg_start = align_down(phdr->p_vaddr, PAGE_SIZE);
    uint64_t seg_end = align_up(mem_end, PAGE_SIZE);

    if (range_overlaps_stack(seg_start, seg_end)) {
        kernel_panic("init ELF segment overlaps user stack");
    }

    uint64_t flags = segment_vmm_flags(phdr);
    log_debug("ELF load: vaddr=%x filesz=%u memsz=%u flags=%x\n",
              phdr->p_vaddr, phdr->p_filesz, phdr->p_memsz, phdr->p_flags);

    for (uint64_t page = seg_start; page < seg_end; page += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_page();
        uint8_t *dst = phys_to_virt(phys);

        uint64_t copy_start = max_u64(page, phdr->p_vaddr);
        uint64_t copy_end = min_u64(page + PAGE_SIZE, file_end);

        if (copy_end > copy_start) {
            uint64_t dst_offset = copy_start - page;
            uint64_t src_offset = phdr->p_offset + (copy_start - phdr->p_vaddr);
            memcpy_local(dst + dst_offset, image + src_offset, copy_end - copy_start);
        }

        if (vmm_map_page_in(cr3_phys, page, phys, flags) != 0) {
            kernel_panic("failed to map init ELF segment page");
        }
    }
}

static void map_user_stack(uint64_t cr3_phys) {
    uint64_t stack_start = USER_STACK_TOP - USER_STACK_SIZE;

    for (uint64_t page = stack_start; page < USER_STACK_TOP; page += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_page();
        if (vmm_map_page_in(cr3_phys, page, phys,
                            VMM_FLAG_WRITABLE | VMM_FLAG_USER | VMM_FLAG_NO_EXEC) != 0) {
            kernel_panic("failed to map init user stack");
        }
    }
}

static struct user_image load_elf_image(const uint8_t *image, uint64_t image_size,
                                        uint64_t cr3_phys, const char *label) {
    const struct elf64_ehdr *ehdr = (const struct elf64_ehdr *)image;

    validate_elf_header(ehdr, image_size);

    log_debug("%s ELF: entry=%x phdrs=%u size=%u\n",
              label, ehdr->e_entry, (uint64_t)ehdr->e_phnum, image_size);

    const struct elf64_phdr *phdrs =
        (const struct elf64_phdr *)(image + ehdr->e_phoff);

    uint64_t image_end = 0;
    for (uint64_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            map_elf_segment(cr3_phys, image, image_size, &phdrs[i]);
            image_end = max_u64(image_end, phdrs[i].p_vaddr + phdrs[i].p_memsz);
        }
    }

    struct user_image loaded = {
        .entry = ehdr->e_entry,
        .brk_start = align_up(image_end, PAGE_SIZE),
    };
    log_debug("%s ELF heap start: %x\n", label, loaded.brk_start);
    return loaded;
}

void user_init_from_modules(struct limine_module_response *modules) {
    struct limine_file *init_module = find_init_module(modules);
    uint64_t cr3_phys = vmm_create_address_space();
    struct user_image image = load_elf_image(init_module->address, init_module->size,
                                             cr3_phys, "init");

    map_user_stack(cr3_phys);

    uint64_t heap_limit = image.brk_start + USER_HEAP_SIZE;
    uint64_t stack_start = USER_STACK_TOP - USER_STACK_SIZE;
    if (heap_limit > stack_start - PAGE_SIZE) {
        heap_limit = stack_start - PAGE_SIZE;
    }

    if (scheduler_create_user_task("init", cr3_phys, image.entry, USER_STACK_TOP, 0, 0,
                                   image.brk_start, heap_limit) != 0) {
        kernel_panic("failed to create init task");
    }

    log_info("init process loaded: cr3=%x entry=%x stack=%x heap=%x..%x\n",
             cr3_phys, image.entry, USER_STACK_TOP, image.brk_start, heap_limit);
}

int user_spawn_from_vfs_with_args(const char *path, const char *args) {
    if (path == 0) {
        return -1;
    }

    uint64_t image_size = vfs_file_size(path);
    if (image_size == (uint64_t)-1 || image_size == 0 || image_size > USER_SPAWN_IMAGE_MAX) {
        return -1;
    }

    uint8_t *image_buffer = kmalloc((size_t)image_size);
    if (image_buffer == 0) {
        return -1;
    }

    uint64_t read = vfs_read_file(path, 0, image_buffer, image_size);
    if (read != image_size) {
        kfree(image_buffer);
        return -1;
    }

    uint64_t cr3_phys = vmm_create_address_space();
    struct user_image image = load_elf_image(image_buffer, image_size, cr3_phys, path);
    map_user_stack(cr3_phys);

    uint64_t user_stack_top = USER_STACK_TOP;
    uint64_t argc = 0;
    uint64_t argv = 0;
    if (build_initial_user_stack(cr3_phys, path, args, &user_stack_top, &argc, &argv) != 0) {
        kfree(image_buffer);
        vmm_destroy_user_address_space(cr3_phys);
        return -1;
    }

    uint64_t heap_limit = image.brk_start + USER_HEAP_SIZE;
    uint64_t stack_start = USER_STACK_TOP - USER_STACK_SIZE;
    if (heap_limit > stack_start - PAGE_SIZE) {
        heap_limit = stack_start - PAGE_SIZE;
    }

    char task_name[USER_TASK_NAME_MAX];
    copy_task_name(task_name, path);
    int result = scheduler_create_user_task(task_name, cr3_phys, image.entry, user_stack_top,
                                            argc, argv,
                                            image.brk_start, heap_limit);
    kfree(image_buffer);
    if (result != 0) {
        vmm_destroy_user_address_space(cr3_phys);
        return -1;
    }

    log_info("spawned user program: path=%s argc=%u cr3=%x entry=%x heap=%x..%x\n",
             path, argc, cr3_phys, image.entry, image.brk_start, heap_limit);
    return 0;
}

int user_spawn_from_vfs(const char *path) {
    return user_spawn_from_vfs_with_args(path, "");
}
