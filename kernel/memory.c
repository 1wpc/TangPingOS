#include <console.h>
#include <limine.h>
#include <log.h>
#include <memory.h>
#include <stddef.h>
#include <stdint.h>

#define PMM_MAX_MEMORY (16ULL * 1024ULL * 1024ULL * 1024ULL)
#define PMM_MAX_PAGES (PMM_MAX_MEMORY / PAGE_SIZE)
#define BITMAP_WORD_BITS 64
#define BITMAP_WORDS (PMM_MAX_PAGES / BITMAP_WORD_BITS)

#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITABLE (1ULL << 1)
#define PTE_USER     (1ULL << 2)
#define PTE_PWT      (1ULL << 3)
#define PTE_PCD      (1ULL << 4)
#define PTE_HUGE     (1ULL << 7)
#define PTE_PAT_HUGE (1ULL << 12)
#define PTE_NO_EXEC  (1ULL << 63)
#define PTE_ADDR_MASK 0x000ffffffffff000ULL
#define USER_VIRT_MAX 0x0000800000000000ULL

#define HEAP_BASE 0xffffffff90000000ULL
#define HEAP_SIZE (16ULL * 1024ULL * 1024ULL)
#define HEAP_ALIGN 16ULL
#define HEAP_MAGIC 0x54484541504d454dULL

static uint64_t pmm_bitmap[BITMAP_WORDS];
static uint64_t pmm_max_page_index;
static uint64_t pmm_managed_page_count;
static uint64_t pmm_free_page_count;
static uint64_t pmm_next_hint;
static uint64_t hhdm_base;
static uint64_t kernel_cr3;
static uint64_t heap_next = HEAP_BASE;
static uint64_t heap_mapped_end = HEAP_BASE;

struct heap_block {
    uint64_t size;
    int free;
    uint64_t magic;
    struct heap_block *prev;
    struct heap_block *next;
};

static struct heap_block *heap_head;
static struct heap_block *heap_tail;

static uint64_t align_up(uint64_t value, uint64_t align) {
    return (value + align - 1) & ~(align - 1);
}

static uint64_t align_down(uint64_t value, uint64_t align) {
    return value & ~(align - 1);
}

static void *memset_local(void *ptr, int value, size_t size) {
    uint8_t *bytes = ptr;
    for (size_t i = 0; i < size; i++) {
        bytes[i] = (uint8_t)value;
    }
    return ptr;
}

static uint64_t heap_header_size(void) {
    return align_up(sizeof(struct heap_block), HEAP_ALIGN);
}

static int pmm_test_bit(uint64_t page) {
    return (pmm_bitmap[page / BITMAP_WORD_BITS] & (1ULL << (page % BITMAP_WORD_BITS))) != 0;
}

static void pmm_set_used(uint64_t page) {
    if (!pmm_test_bit(page)) {
        pmm_free_page_count--;
    }
    pmm_bitmap[page / BITMAP_WORD_BITS] |= (1ULL << (page % BITMAP_WORD_BITS));
}

static void pmm_set_free(uint64_t page) {
    if (pmm_test_bit(page)) {
        pmm_free_page_count++;
    }
    pmm_bitmap[page / BITMAP_WORD_BITS] &= ~(1ULL << (page % BITMAP_WORD_BITS));
}

static void pmm_mark_range_free(uint64_t base, uint64_t length) {
    uint64_t start = align_up(base, PAGE_SIZE);
    uint64_t end = align_down(base + length, PAGE_SIZE);

    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
        uint64_t page = addr / PAGE_SIZE;
        if (page < PMM_MAX_PAGES) {
            pmm_set_free(page);
        }
    }
}

void *phys_to_virt(uint64_t phys_addr) {
    return (void *)(hhdm_base + phys_addr);
}

uint64_t pmm_alloc_page(void) {
    for (uint64_t pass = 0; pass < 2; pass++) {
        uint64_t start = pass == 0 ? pmm_next_hint : 0;
        uint64_t end = pass == 0 ? pmm_max_page_index : pmm_next_hint;

        for (uint64_t page = start; page < end; page++) {
            if (!pmm_test_bit(page)) {
                pmm_set_used(page);
                pmm_next_hint = page + 1;
                uint64_t phys = page * PAGE_SIZE;
                memset_local(phys_to_virt(phys), 0, PAGE_SIZE);
                return phys;
            }
        }
    }

    kernel_panic("out of physical pages");
    return 0;
}

uint64_t pmm_alloc_contiguous_pages(uint64_t count) {
    if (count == 0) {
        return 0;
    }

    for (uint64_t start = pmm_next_hint; start + count <= pmm_max_page_index; start++) {
        int free = 1;
        for (uint64_t i = 0; i < count; i++) {
            if (pmm_test_bit(start + i)) {
                free = 0;
                start += i;
                break;
            }
        }
        if (!free) {
            continue;
        }

        for (uint64_t i = 0; i < count; i++) {
            pmm_set_used(start + i);
            memset_local(phys_to_virt((start + i) * PAGE_SIZE), 0, PAGE_SIZE);
        }
        pmm_next_hint = start + count;
        return start * PAGE_SIZE;
    }

    for (uint64_t start = 0; start + count <= pmm_next_hint; start++) {
        int free = 1;
        for (uint64_t i = 0; i < count; i++) {
            if (pmm_test_bit(start + i)) {
                free = 0;
                start += i;
                break;
            }
        }
        if (!free) {
            continue;
        }

        for (uint64_t i = 0; i < count; i++) {
            pmm_set_used(start + i);
            memset_local(phys_to_virt((start + i) * PAGE_SIZE), 0, PAGE_SIZE);
        }
        pmm_next_hint = start + count;
        return start * PAGE_SIZE;
    }

    kernel_panic("out of contiguous physical pages");
    return 0;
}

void pmm_free_page(uint64_t phys_addr) {
    if ((phys_addr % PAGE_SIZE) != 0) {
        kernel_panic("attempted to free unaligned physical page");
    }

    uint64_t page = phys_addr / PAGE_SIZE;
    if (page >= pmm_max_page_index) {
        kernel_panic("attempted to free out-of-range physical page");
    }

    pmm_set_free(page);
    if (page < pmm_next_hint) {
        pmm_next_hint = page;
    }
}

uint64_t pmm_total_pages(void) {
    return pmm_managed_page_count;
}

uint64_t pmm_free_pages(void) {
    return pmm_free_page_count;
}

static inline uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}

static inline void invlpg(uint64_t virt_addr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "memory");
}

static void pat_enable_write_combining(void) {
    const uint32_t ia32_pat = 0x277;
    const uint64_t pat_type_wc = 0x01;
    uint64_t pat = rdmsr(ia32_pat);

    pat &= ~(0xffULL << 8);
    pat |= pat_type_wc << 8;
    wrmsr(ia32_pat, pat);
    __asm__ volatile ("wbinvd" : : : "memory");
}

static uint64_t *vmm_next_level(uint64_t *table, uint64_t index, int create, uint64_t flags) {
    if ((table[index] & PTE_PRESENT) == 0) {
        if (!create) {
            return NULL;
        }

        uint64_t phys = pmm_alloc_page();
        table[index] = phys | PTE_PRESENT | PTE_WRITABLE | (flags & PTE_USER);
    } else if ((flags & PTE_USER) != 0) {
        table[index] |= PTE_USER;
    }

    return phys_to_virt(table[index] & PTE_ADDR_MASK);
}

int vmm_map_page_in(uint64_t cr3_phys, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    if ((virt_addr % PAGE_SIZE) != 0 || (phys_addr % PAGE_SIZE) != 0) {
        return -1;
    }

    uint64_t pml4_i = (virt_addr >> 39) & 0x1ff;
    uint64_t pdpt_i = (virt_addr >> 30) & 0x1ff;
    uint64_t pd_i = (virt_addr >> 21) & 0x1ff;
    uint64_t pt_i = (virt_addr >> 12) & 0x1ff;

    uint64_t *pml4 = phys_to_virt(cr3_phys & PTE_ADDR_MASK);
    uint64_t *pdpt = vmm_next_level(pml4, pml4_i, 1, flags);
    uint64_t *pd = vmm_next_level(pdpt, pdpt_i, 1, flags);
    uint64_t *pt = vmm_next_level(pd, pd_i, 1, flags);

    uint64_t pte_flags = flags;
    if ((pte_flags & VMM_FLAG_WRITE_COMBINING) != 0) {
        pte_flags &= ~VMM_FLAG_WRITE_COMBINING;
        pte_flags &= ~PTE_PCD;
        pte_flags |= PTE_PWT;
    }

    pt[pt_i] = (phys_addr & PTE_ADDR_MASK) | pte_flags | PTE_PRESENT;
    if ((read_cr3() & PTE_ADDR_MASK) == (cr3_phys & PTE_ADDR_MASK)) {
        invlpg(virt_addr);
    }
    return 0;
}

int vmm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    return vmm_map_page_in(read_cr3(), virt_addr, phys_addr, flags);
}

int vmm_translate_kernel_addr(uint64_t virt_addr, uint64_t *phys_out) {
    if (phys_out == NULL) {
        return -1;
    }

    uint64_t pml4_i = (virt_addr >> 39) & 0x1ff;
    uint64_t pdpt_i = (virt_addr >> 30) & 0x1ff;
    uint64_t pd_i = (virt_addr >> 21) & 0x1ff;
    uint64_t pt_i = (virt_addr >> 12) & 0x1ff;

    uint64_t *pml4 = phys_to_virt((read_cr3() & PTE_ADDR_MASK));
    uint64_t pml4e = pml4[pml4_i];
    if ((pml4e & PTE_PRESENT) == 0) {
        return -1;
    }

    uint64_t *pdpt = phys_to_virt(pml4e & PTE_ADDR_MASK);
    uint64_t pdpte = pdpt[pdpt_i];
    if ((pdpte & PTE_PRESENT) == 0) {
        return -1;
    }
    if ((pdpte & PTE_HUGE) != 0) {
        *phys_out = (pdpte & 0x000fffffc0000000ULL) | (virt_addr & 0x3fffffffULL);
        return 0;
    }

    uint64_t *pd = phys_to_virt(pdpte & PTE_ADDR_MASK);
    uint64_t pde = pd[pd_i];
    if ((pde & PTE_PRESENT) == 0) {
        return -1;
    }
    if ((pde & PTE_HUGE) != 0) {
        *phys_out = (pde & 0x000fffffffe00000ULL) | (virt_addr & 0x1fffffULL);
        return 0;
    }

    uint64_t *pt = phys_to_virt(pde & PTE_ADDR_MASK);
    uint64_t pte = pt[pt_i];
    if ((pte & PTE_PRESENT) == 0) {
        return -1;
    }

    *phys_out = (pte & PTE_ADDR_MASK) | (virt_addr & (PAGE_SIZE - 1));
    return 0;
}

int vmm_translate_user_addr(uint64_t cr3_phys, uint64_t virt_addr, int write, uint64_t *phys_out) {
    if (phys_out == NULL || virt_addr >= USER_VIRT_MAX) {
        return -1;
    }

    uint64_t pml4_i = (virt_addr >> 39) & 0x1ff;
    uint64_t pdpt_i = (virt_addr >> 30) & 0x1ff;
    uint64_t pd_i = (virt_addr >> 21) & 0x1ff;
    uint64_t pt_i = (virt_addr >> 12) & 0x1ff;
    uint64_t indices[4] = {pml4_i, pdpt_i, pd_i, pt_i};

    uint64_t *table = phys_to_virt(cr3_phys & PTE_ADDR_MASK);
    for (uint64_t level = 0; level < 4; level++) {
        uint64_t entry = table[indices[level]];
        if ((entry & PTE_PRESENT) == 0 || (entry & PTE_USER) == 0) {
            return -1;
        }
        if (write && (entry & PTE_WRITABLE) == 0) {
            return -1;
        }

        if (level == 3) {
            *phys_out = (entry & PTE_ADDR_MASK) | (virt_addr & (PAGE_SIZE - 1));
            return 0;
        }

        if ((entry & PTE_HUGE) != 0) {
            return -1;
        }
        table = phys_to_virt(entry & PTE_ADDR_MASK);
    }

    return -1;
}

static uint64_t vmm_free_pt(uint64_t pt_phys) {
    uint64_t freed = 0;
    uint64_t *pt = phys_to_virt(pt_phys & PTE_ADDR_MASK);

    for (uint64_t i = 0; i < 512; i++) {
        uint64_t entry = pt[i];
        if ((entry & PTE_PRESENT) == 0) {
            continue;
        }

        pmm_free_page(entry & PTE_ADDR_MASK);
        pt[i] = 0;
        freed++;
    }

    pmm_free_page(pt_phys & PTE_ADDR_MASK);
    return freed + 1;
}

static uint64_t vmm_free_pd(uint64_t pd_phys) {
    uint64_t freed = 0;
    uint64_t *pd = phys_to_virt(pd_phys & PTE_ADDR_MASK);

    for (uint64_t i = 0; i < 512; i++) {
        uint64_t entry = pd[i];
        if ((entry & PTE_PRESENT) == 0) {
            continue;
        }
        if ((entry & PTE_HUGE) != 0) {
            kernel_panic("cannot free huge user PD mapping");
        }

        freed += vmm_free_pt(entry & PTE_ADDR_MASK);
        pd[i] = 0;
    }

    pmm_free_page(pd_phys & PTE_ADDR_MASK);
    return freed + 1;
}

static uint64_t vmm_free_pdpt(uint64_t pdpt_phys) {
    uint64_t freed = 0;
    uint64_t *pdpt = phys_to_virt(pdpt_phys & PTE_ADDR_MASK);

    for (uint64_t i = 0; i < 512; i++) {
        uint64_t entry = pdpt[i];
        if ((entry & PTE_PRESENT) == 0) {
            continue;
        }
        if ((entry & PTE_HUGE) != 0) {
            kernel_panic("cannot free huge user PDPT mapping");
        }

        freed += vmm_free_pd(entry & PTE_ADDR_MASK);
        pdpt[i] = 0;
    }

    pmm_free_page(pdpt_phys & PTE_ADDR_MASK);
    return freed + 1;
}

uint64_t vmm_destroy_user_address_space(uint64_t cr3_phys) {
    cr3_phys &= PTE_ADDR_MASK;
    if (cr3_phys == 0 || cr3_phys == kernel_cr3 || cr3_phys == (read_cr3() & PTE_ADDR_MASK)) {
        return 0;
    }

    uint64_t freed = 0;
    uint64_t *pml4 = phys_to_virt(cr3_phys);

    for (uint64_t i = 0; i < 256; i++) {
        uint64_t entry = pml4[i];
        if ((entry & PTE_PRESENT) == 0) {
            continue;
        }

        freed += vmm_free_pdpt(entry & PTE_ADDR_MASK);
        pml4[i] = 0;
    }

    pmm_free_page(cr3_phys);
    return freed + 1;
}

uint64_t vmm_create_address_space(void) {
    uint64_t cr3_phys = pmm_alloc_page();
    uint64_t *new_pml4 = phys_to_virt(cr3_phys);
    uint64_t *kernel_pml4 = phys_to_virt(kernel_cr3);

    for (uint64_t i = 0; i < 256; i++) {
        new_pml4[i] = 0;
    }

    for (uint64_t i = 256; i < 512; i++) {
        new_pml4[i] = kernel_pml4[i];
    }

    log_debug("address space created: cr3=%x\n", cr3_phys);
    return cr3_phys;
}

uint64_t vmm_current_address_space(void) {
    return read_cr3() & PTE_ADDR_MASK;
}

uint64_t vmm_kernel_address_space(void) {
    return kernel_cr3;
}

void vmm_switch_address_space(uint64_t cr3_phys) {
    cr3_phys &= PTE_ADDR_MASK;
    if ((read_cr3() & PTE_ADDR_MASK) == cr3_phys) {
        return;
    }
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3_phys) : "memory");
}

static void heap_ensure_mapped(uint64_t end) {
    while (heap_mapped_end < end) {
        uint64_t phys = pmm_alloc_page();
        if (vmm_map_page(heap_mapped_end, phys, PTE_WRITABLE | PTE_NO_EXEC) != 0) {
            kernel_panic("failed to map kernel heap page");
        }
        heap_mapped_end += PAGE_SIZE;
    }
}

static void heap_split_block(struct heap_block *block, uint64_t size) {
    uint64_t header_size = heap_header_size();

    if (block->size < size + header_size + HEAP_ALIGN) {
        return;
    }

    struct heap_block *split = (struct heap_block *)((uint8_t *)block + header_size + size);
    split->size = block->size - size - header_size;
    split->free = 1;
    split->magic = HEAP_MAGIC;
    split->prev = block;
    split->next = block->next;

    if (split->next != NULL) {
        split->next->prev = split;
    } else {
        heap_tail = split;
    }

    block->size = size;
    block->next = split;
}

static void heap_coalesce(struct heap_block *block) {
    uint64_t header_size = heap_header_size();

    if (block->next != NULL && block->next->free) {
        struct heap_block *next = block->next;
        block->size += header_size + next->size;
        block->next = next->next;
        if (block->next != NULL) {
            block->next->prev = block;
        } else {
            heap_tail = block;
        }
    }

    if (block->prev != NULL && block->prev->free) {
        struct heap_block *prev = block->prev;
        prev->size += header_size + block->size;
        prev->next = block->next;
        if (prev->next != NULL) {
            prev->next->prev = prev;
        } else {
            heap_tail = prev;
        }
    }
}

void *kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    uint64_t alloc_size = align_up(size, HEAP_ALIGN);
    uint64_t header_size = heap_header_size();

    for (struct heap_block *block = heap_head; block != NULL; block = block->next) {
        if (block->free && block->size >= alloc_size) {
            heap_split_block(block, alloc_size);
            block->free = 0;
            return (uint8_t *)block + header_size;
        }
    }

    uint64_t start = align_up(heap_next, HEAP_ALIGN);
    uint64_t end = align_up(start + header_size + alloc_size, HEAP_ALIGN);
    if (end > HEAP_BASE + HEAP_SIZE) {
        kernel_panic("kernel heap exhausted");
    }

    heap_ensure_mapped(end);

    struct heap_block *block = (struct heap_block *)start;
    block->size = alloc_size;
    block->free = 0;
    block->magic = HEAP_MAGIC;
    block->prev = heap_tail;
    block->next = NULL;

    if (heap_tail != NULL) {
        heap_tail->next = block;
    } else {
        heap_head = block;
    }
    heap_tail = block;

    heap_next = end;
    return (uint8_t *)block + header_size;
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    memset_local(ptr, 0, size);
    return ptr;
}

void kfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    uint64_t header_size = heap_header_size();
    struct heap_block *block = (struct heap_block *)((uint8_t *)ptr - header_size);
    if (block->magic != HEAP_MAGIC || block->free) {
        kernel_panic("invalid kernel heap free");
    }

    block->free = 1;
    heap_coalesce(block);
}

void memory_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset) {
    hhdm_base = hhdm_offset;
    kernel_cr3 = read_cr3() & PTE_ADDR_MASK;
    pat_enable_write_combining();
    pmm_free_page_count = 0;
    pmm_managed_page_count = 0;
    pmm_max_page_index = 0;
    pmm_next_hint = 0;

    for (uint64_t i = 0; i < BITMAP_WORDS; i++) {
        pmm_bitmap[i] = UINT64_MAX;
    }

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        uint64_t end = entry->base + entry->length;
        uint64_t page_end = align_up(end, PAGE_SIZE) / PAGE_SIZE;

        if (page_end > pmm_max_page_index) {
            pmm_max_page_index = page_end;
            if (pmm_max_page_index > PMM_MAX_PAGES) {
                pmm_max_page_index = PMM_MAX_PAGES;
            }
        }

        uint64_t managed_start = align_down(entry->base, PAGE_SIZE) / PAGE_SIZE;
        uint64_t managed_end = page_end;
        if (managed_start < PMM_MAX_PAGES) {
            if (managed_end > PMM_MAX_PAGES) {
                managed_end = PMM_MAX_PAGES;
            }
            if (managed_end > managed_start) {
                pmm_managed_page_count += managed_end - managed_start;
            }
        }

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            pmm_mark_range_free(entry->base, entry->length);
        }
    }

    log_info("HHDM offset: %x\n", hhdm_base);
    log_info("kernel address space: cr3=%x\n", kernel_cr3);
    log_info("PAT configured: PWT cache index uses write-combining\n");
    log_info("PMM initialized: managed pages %u free pages %u\n",
             pmm_managed_page_count, pmm_free_page_count);
}

void memory_self_test(void) {
    uint64_t before = pmm_free_pages();
    uint64_t page = pmm_alloc_page();
    volatile uint64_t *mapped = phys_to_virt(page);
    mapped[0] = 0x1122334455667788ULL;
    if (mapped[0] != 0x1122334455667788ULL) {
        kernel_panic("PMM direct map self-test failed");
    }
    pmm_free_page(page);

    uint64_t test_phys = pmm_alloc_page();
    uint64_t test_virt = 0xffffffff91000000ULL;
    if (vmm_map_page(test_virt, test_phys, PTE_WRITABLE | PTE_NO_EXEC) != 0) {
        kernel_panic("VMM map self-test failed");
    }

    volatile uint64_t *test_page = (volatile uint64_t *)test_virt;
    test_page[0] = 0xaabbccddeeff0011ULL;
    if (test_page[0] != 0xaabbccddeeff0011ULL) {
        kernel_panic("VMM write self-test failed");
    }

    uint64_t *heap_block = kmalloc(128);
    heap_block[0] = 0xcafebabedeadbeefULL;
    heap_block[15] = 0x123456789abcdef0ULL;
    if (heap_block[0] != 0xcafebabedeadbeefULL ||
        heap_block[15] != 0x123456789abcdef0ULL) {
        kernel_panic("heap self-test failed");
    }
    kfree(heap_block);

    void *reuse_a = kmalloc(64);
    kfree(reuse_a);
    void *reuse_b = kmalloc(64);
    if (reuse_a != reuse_b) {
        kernel_panic("heap reuse self-test failed");
    }
    kfree(reuse_b);

    log_info("memory self-test OK: free pages %u -> %u\n",
             before, pmm_free_pages());
}
