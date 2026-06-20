#include <console.h>
#include <devfs.h>
#include <initrd.h>
#include <limine.h>
#include <memory.h>
#include <ramfs.h>
#include <scheduler.h>
#include <sysinfo.h>
#include <stdint.h>
#include <user.h>
#include <vfs.h>
#include <x86_64/arch.h>

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[3] = LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start_marker[4] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end_marker[2] = LIMINE_REQUESTS_END_MARKER;

void _start(void) {
    console_init_serial();
    console_write("TangPingOS serial online\n");

    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision)) {
        kernel_halt();
    }

    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        kernel_panic("no framebuffer from bootloader");
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    console_init_framebuffer(fb);

    console_write("TangPingOS booted\n");
    console_write("UEFI framebuffer OK\n");
    console_write("framebuffer: ");
    console_write_u64(fb->width);
    console_write("x");
    console_write_u64(fb->height);
    console_write(" pitch ");
    console_write_u64(fb->pitch);
    console_write(" bpp ");
    console_write_u64(fb->bpp);
    console_write("\n");

    if (memmap_request.response == NULL) {
        kernel_panic("no memory map from bootloader");
    }

    console_write("memory map loaded: ");
    console_write_u64(memmap_request.response->entry_count);
    console_write(" entries\n");

    for (uint64_t i = 0; i < memmap_request.response->entry_count && i < 6; i++) {
        struct limine_memmap_entry *entry = memmap_request.response->entries[i];
        console_write("mem ");
        console_write_u64(i);
        console_write(": base ");
        console_write_hex(entry->base);
        console_write(" len ");
        console_write_hex(entry->length);
        console_write(" type ");
        console_write_u64(entry->type);
        console_write("\n");
    }

    if (hhdm_request.response == NULL) {
        kernel_panic("no HHDM from bootloader");
    }

    sysinfo_init(fb, memmap_request.response);
    memory_init(memmap_request.response, hhdm_request.response->offset);
    memory_self_test();

    x86_64_interrupts_init();
    scheduler_init();
    vfs_init();
    devfs_init();
    ramfs_init();
    initrd_init(module_request.response);
    user_init_from_modules(module_request.response);
    scheduler_dump_tasks();
    x86_64_interrupts_enable();

#ifdef TANGPINGOS_TEST_EXCEPTION
    console_write("triggering test exception: invalid opcode\n");
    __asm__ volatile ("ud2");
#endif

#ifdef TANGPINGOS_TEST_PAGE_FAULT
    console_write("triggering test page fault\n");
    *(volatile uint64_t *)0xffff8000dead0000ULL = 0x55;
#endif

    console_write("kernel idle loop entered\n");
    kernel_halt();
}
