#ifndef TANGPINGOS_SYSINFO_H
#define TANGPINGOS_SYSINFO_H

#include <limine.h>
#include <stdint.h>

#define SYSINFO_CPU_NAME_MAX 64

struct sys_meminfo {
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t used_pages;
    uint64_t page_size;
};

struct sys_system_info {
    uint64_t framebuffer_width;
    uint64_t framebuffer_height;
    uint64_t framebuffer_pitch;
    uint64_t framebuffer_bpp;
    uint64_t memmap_entries;
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t used_pages;
    uint64_t page_size;
    uint64_t ticks;
    uint64_t timer_hz;
    uint64_t xhci_found;
    uint64_t xhci_count;
    uint64_t xhci_bus;
    uint64_t xhci_slot;
    uint64_t xhci_function;
    uint64_t xhci_vendor_id;
    uint64_t xhci_device_id;
    uint64_t xhci_revision;
    uint64_t xhci_irq_line;
    uint64_t xhci_bar0_raw;
    uint64_t xhci_mmio_base;
    uint64_t xhci_bar0_is_64bit;
    char cpu_name[SYSINFO_CPU_NAME_MAX];
};

void sysinfo_init(struct limine_framebuffer *framebuffer, struct limine_memmap_response *memmap);
void sysinfo_get_meminfo(struct sys_meminfo *out);
void sysinfo_get_system_info(struct sys_system_info *out);

#endif
