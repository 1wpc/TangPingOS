#include <memory.h>
#include <stdint.h>
#include <usercopy.h>

static uint64_t min_u64(uint64_t a, uint64_t b) {
    return a < b ? a : b;
}

static int range_wraps(uint64_t start, uint64_t len) {
    return len != 0 && start + len - 1 < start;
}

int usercopy_validate_range(const void *user_ptr, uint64_t len, int write) {
    uint64_t base = (uint64_t)user_ptr;
    uint64_t checked = 0;
    uint64_t cr3 = vmm_current_address_space();

    if (len == 0) {
        return 0;
    }
    if (range_wraps(base, len)) {
        return -1;
    }

    while (checked < len) {
        uint64_t addr = base + checked;
        uint64_t phys;
        if (vmm_translate_user_addr(cr3, addr, write, &phys) != 0) {
            return -1;
        }

        uint64_t page_remaining = PAGE_SIZE - (addr & (PAGE_SIZE - 1));
        checked += min_u64(page_remaining, len - checked);
    }

    return 0;
}

int copy_from_user(void *dst, const void *user_src, uint64_t len) {
    uint8_t *out = (uint8_t *)dst;
    uint64_t base = (uint64_t)user_src;
    uint64_t copied = 0;
    uint64_t cr3 = vmm_current_address_space();

    if (len == 0) {
        return 0;
    }
    if (dst == 0 || user_src == 0 || range_wraps(base, len)) {
        return -1;
    }

    while (copied < len) {
        uint64_t addr = base + copied;
        uint64_t phys;
        if (vmm_translate_user_addr(cr3, addr, 0, &phys) != 0) {
            return -1;
        }

        uint64_t page_remaining = PAGE_SIZE - (addr & (PAGE_SIZE - 1));
        uint64_t to_copy = min_u64(page_remaining, len - copied);
        const uint8_t *in = (const uint8_t *)phys_to_virt(phys);

        for (uint64_t i = 0; i < to_copy; i++) {
            out[copied + i] = in[i];
        }

        copied += to_copy;
    }

    return 0;
}

int copy_to_user(void *user_dst, const void *src, uint64_t len) {
    uint64_t base = (uint64_t)user_dst;
    const uint8_t *in = (const uint8_t *)src;
    uint64_t copied = 0;
    uint64_t cr3 = vmm_current_address_space();

    if (len == 0) {
        return 0;
    }
    if (user_dst == 0 || src == 0 || range_wraps(base, len)) {
        return -1;
    }

    while (copied < len) {
        uint64_t addr = base + copied;
        uint64_t phys;
        if (vmm_translate_user_addr(cr3, addr, 1, &phys) != 0) {
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

int copy_string_from_user(char *dst, const char *user_src, uint64_t max_len) {
    uint64_t cr3 = vmm_current_address_space();
    uint64_t base = (uint64_t)user_src;

    if (dst == 0 || user_src == 0 || max_len == 0) {
        return -1;
    }

    for (uint64_t i = 0; i < max_len; i++) {
        uint64_t phys;
        if (vmm_translate_user_addr(cr3, base + i, 0, &phys) != 0) {
            return -1;
        }

        char c = *(const char *)phys_to_virt(phys);
        dst[i] = c;
        if (c == '\0') {
            return 0;
        }
    }

    dst[max_len - 1] = '\0';
    return -1;
}
