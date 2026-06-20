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
    char cpu_name[SYSINFO_CPU_NAME_MAX];
};

void sysinfo_init(struct limine_framebuffer *framebuffer, struct limine_memmap_response *memmap);
void sysinfo_get_meminfo(struct sys_meminfo *out);
void sysinfo_get_system_info(struct sys_system_info *out);

#endif
