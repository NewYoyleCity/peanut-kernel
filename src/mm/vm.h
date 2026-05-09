#ifndef VM_H
#define VM_H

#include "freelib/kstdint.h"

#define VM_PAGE_SIZE 4096ull
#define VM_FLAG_PRESENT 0x001ull
#define VM_FLAG_WRITE   0x002ull
#define VM_FLAG_USER    0x004ull
#define VM_FLAG_HUGE    0x080ull

void vm_init(void);
void* vm_alloc_page(void);
void vm_free_page(void* page);
uint64_t vm_kernel_cr3(void);
int vm_map_identity_2m(uint64_t start, uint64_t bytes, uint64_t flags);
int vm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
uint64_t vm_virt_to_phys(uint64_t virt);

#endif
