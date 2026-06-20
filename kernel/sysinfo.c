#include <memory.h>
#include <scheduler.h>
#include <stdint.h>
#include <sysinfo.h>

#define PIT_HZ 100ULL

static struct sys_system_info boot_info;

static void zero_bytes(void *ptr, uint64_t len) {
    uint8_t *out = ptr;
    for (uint64_t i = 0; i < len; i++) {
        out[i] = 0;
    }
}

static void copy_bytes(char *dst, const char *src, uint64_t max_len) {
    uint64_t i = 0;
    if (max_len == 0) {
        return;
    }
    while (i + 1 < max_len && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void copy_raw(void *dst, const void *src, uint64_t len) {
    uint8_t *out = dst;
    const uint8_t *in = src;
    for (uint64_t i = 0; i < len; i++) {
        out[i] = in[i];
    }
}

static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile (
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0)
    );
}

static void detect_cpu_name(char *out, uint64_t out_len) {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t max_extended;

    if (out_len == 0) {
        return;
    }

    cpuid(0x80000000U, &max_extended, &ebx, &ecx, &edx);
    if (max_extended >= 0x80000004U && out_len >= 49) {
        uint32_t *brand = (uint32_t *)out;
        for (uint32_t leaf = 0; leaf < 3; leaf++) {
            cpuid(0x80000002U + leaf,
                  &brand[leaf * 4 + 0],
                  &brand[leaf * 4 + 1],
                  &brand[leaf * 4 + 2],
                  &brand[leaf * 4 + 3]);
        }
        out[48] = '\0';
        return;
    }

    cpuid(0, &eax, &ebx, &ecx, &edx);
    char vendor[13];
    ((uint32_t *)vendor)[0] = ebx;
    ((uint32_t *)vendor)[1] = edx;
    ((uint32_t *)vendor)[2] = ecx;
    vendor[12] = '\0';
    copy_bytes(out, vendor, out_len);
}

void sysinfo_init(struct limine_framebuffer *framebuffer, struct limine_memmap_response *memmap) {
    zero_bytes(&boot_info, sizeof(boot_info));

    if (framebuffer != 0) {
        boot_info.framebuffer_width = framebuffer->width;
        boot_info.framebuffer_height = framebuffer->height;
        boot_info.framebuffer_pitch = framebuffer->pitch;
        boot_info.framebuffer_bpp = framebuffer->bpp;
    }
    if (memmap != 0) {
        boot_info.memmap_entries = memmap->entry_count;
    }
    boot_info.page_size = PAGE_SIZE;
    boot_info.timer_hz = PIT_HZ;
    detect_cpu_name(boot_info.cpu_name, sizeof(boot_info.cpu_name));
}

void sysinfo_get_meminfo(struct sys_meminfo *out) {
    if (out == 0) {
        return;
    }

    out->total_pages = pmm_total_pages();
    out->free_pages = pmm_free_pages();
    out->used_pages = out->total_pages - out->free_pages;
    out->page_size = PAGE_SIZE;
}

void sysinfo_get_system_info(struct sys_system_info *out) {
    if (out == 0) {
        return;
    }

    copy_raw(out, &boot_info, sizeof(*out));
    out->total_pages = pmm_total_pages();
    out->free_pages = pmm_free_pages();
    out->used_pages = out->total_pages - out->free_pages;
    out->ticks = scheduler_ticks();
}
