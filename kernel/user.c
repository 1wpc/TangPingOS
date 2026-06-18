#include <console.h>
#include <memory.h>
#include <scheduler.h>
#include <stdint.h>
#include <user.h>

#define USER_CODE_BASE  0x0000400000000000ULL
#define USER_STACK_TOP  0x0000400010000000ULL

extern const uint8_t user_program_start[];
extern const uint8_t user_program_end[];

static void *memcpy_local(void *dst, const void *src, uint64_t len) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (uint64_t i = 0; i < len; i++) {
        d[i] = s[i];
    }
    return dst;
}

void user_demo_init(void) {
    uint64_t program_size = (uint64_t)(user_program_end - user_program_start);
    if (program_size > PAGE_SIZE) {
        kernel_panic("user demo program exceeds one page");
    }

    uint64_t code_phys = pmm_alloc_page();
    memcpy_local(phys_to_virt(code_phys), user_program_start, program_size);

    uint64_t cr3_phys = vmm_create_address_space();
    if (vmm_map_page_in(cr3_phys, USER_CODE_BASE, code_phys, VMM_FLAG_USER) != 0) {
        kernel_panic("failed to map user code page");
    }

    uint64_t stack_phys = pmm_alloc_page();
    if (vmm_map_page_in(cr3_phys, USER_STACK_TOP - PAGE_SIZE, stack_phys,
                        VMM_FLAG_WRITABLE | VMM_FLAG_USER | VMM_FLAG_NO_EXEC) != 0) {
        kernel_panic("failed to map user stack page");
    }

    if (scheduler_create_user_task("user-demo", cr3_phys, USER_CODE_BASE, USER_STACK_TOP) != 0) {
        kernel_panic("failed to create user demo task");
    }

    console_printf("user demo mapped: cr3=%x entry=%x stack=%x bytes=%u\n",
                   cr3_phys, USER_CODE_BASE, USER_STACK_TOP, program_size);
}
