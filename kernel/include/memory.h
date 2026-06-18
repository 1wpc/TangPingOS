#ifndef VOIDOS_MEMORY_H
#define VOIDOS_MEMORY_H

#include <limine.h>
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096ULL
#define VMM_FLAG_WRITABLE (1ULL << 1)
#define VMM_FLAG_USER     (1ULL << 2)
#define VMM_FLAG_NO_EXEC  (1ULL << 63)

void memory_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset);
void memory_self_test(void);
void *phys_to_virt(uint64_t phys_addr);
uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t phys_addr);
uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);
int vmm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
int vmm_map_page_in(uint64_t cr3_phys, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
uint64_t vmm_create_address_space(void);
uint64_t vmm_current_address_space(void);
uint64_t vmm_kernel_address_space(void);
void vmm_switch_address_space(uint64_t cr3_phys);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void kfree(void *ptr);

#endif
