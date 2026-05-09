#include "mm/vm.h"
#include "freelib/kalloc.h"

#define VM_ENTRIES 512
#define VM_BOOT_MAP_BYTES (4ull * 1024ull * 1024ull * 1024ull)

typedef uint64_t PageTable[VM_ENTRIES];

extern uint8_t _kernel_end;

static PageTable kernel_pml4 __attribute__((aligned(VM_PAGE_SIZE)));
static PageTable kernel_pdpt __attribute__((aligned(VM_PAGE_SIZE)));
static PageTable kernel_pd[4] __attribute__((aligned(VM_PAGE_SIZE)));

static uint8_t* page_bump;

static uint64_t align_up(uint64_t v, uint64_t a) {
    return (v + a - 1ull) & ~(a - 1ull);
}

void vm_init(void) {
    page_bump = (uint8_t*)align_up((uint64_t)&_kernel_end, VM_PAGE_SIZE);
    vm_map_identity_2m(0, VM_BOOT_MAP_BYTES, VM_FLAG_PRESENT | VM_FLAG_WRITE | VM_FLAG_USER);
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint64_t)kernel_pml4) : "memory");
}

void* vm_alloc_page(void) {
    uint8_t* page = page_bump;
    page_bump += VM_PAGE_SIZE;
    for (uint32_t i = 0; i < VM_PAGE_SIZE; i++)
        page[i] = 0;
    return page;
}

void vm_free_page(void* page) {
    (void)page;
}

uint64_t vm_kernel_cr3(void) {
    return (uint64_t)kernel_pml4;
}

int vm_map_identity_2m(uint64_t start, uint64_t bytes, uint64_t flags) {
    if ((start & 0x1FFFFFull) || (bytes & 0x1FFFFFull))
        return -1;
    kernel_pml4[0] = (uint64_t)kernel_pdpt | VM_FLAG_PRESENT | VM_FLAG_WRITE | VM_FLAG_USER;
    for (uint32_t i = 0; i < 4; i++)
        kernel_pdpt[i] = (uint64_t)kernel_pd[i] | VM_FLAG_PRESENT | VM_FLAG_WRITE | VM_FLAG_USER;
    uint64_t end = start + bytes;
    for (uint64_t addr = start; addr < end; addr += 0x200000ull) {
        uint32_t pdpt_i = (uint32_t)((addr >> 30) & 0x1ffu);
        uint32_t pd_i = (uint32_t)((addr >> 21) & 0x1ffu);
        if (pdpt_i >= 4)
            return -1;
        kernel_pd[pdpt_i][pd_i] = addr | flags | VM_FLAG_HUGE;
    }
    return 0;
}

int vm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    if ((virt & (VM_PAGE_SIZE - 1ull)) || (phys & (VM_PAGE_SIZE - 1ull)))
        return -1;
    uint32_t pml4_i = (uint32_t)((virt >> 39) & 0x1ffu);
    uint32_t pdpt_i = (uint32_t)((virt >> 30) & 0x1ffu);
    uint32_t pd_i = (uint32_t)((virt >> 21) & 0x1ffu);
    uint32_t pt_i = (uint32_t)((virt >> 12) & 0x1ffu);

    PageTable* pdpt;
    PageTable* pd;
    PageTable* pt;
    if (!(kernel_pml4[pml4_i] & VM_FLAG_PRESENT)) {
        pdpt = (PageTable*)vm_alloc_page();
        kernel_pml4[pml4_i] = (uint64_t)pdpt | VM_FLAG_PRESENT | VM_FLAG_WRITE | VM_FLAG_USER;
    } else {
        pdpt = (PageTable*)(kernel_pml4[pml4_i] & ~0xfffull);
    }
    if (!((*pdpt)[pdpt_i] & VM_FLAG_PRESENT)) {
        pd = (PageTable*)vm_alloc_page();
        (*pdpt)[pdpt_i] = (uint64_t)pd | VM_FLAG_PRESENT | VM_FLAG_WRITE | VM_FLAG_USER;
    } else {
        pd = (PageTable*)((*pdpt)[pdpt_i] & ~0xfffull);
    }
    if ((*pd)[pd_i] & VM_FLAG_HUGE)
        return -1;
    if (!((*pd)[pd_i] & VM_FLAG_PRESENT)) {
        pt = (PageTable*)vm_alloc_page();
        (*pd)[pd_i] = (uint64_t)pt | VM_FLAG_PRESENT | VM_FLAG_WRITE | VM_FLAG_USER;
    } else {
        pt = (PageTable*)((*pd)[pd_i] & ~0xfffull);
    }
    (*pt)[pt_i] = phys | flags | VM_FLAG_PRESENT;
    return 0;
}

uint64_t vm_virt_to_phys(uint64_t virt) {
    uint32_t pml4_i = (uint32_t)((virt >> 39) & 0x1ffu);
    uint32_t pdpt_i = (uint32_t)((virt >> 30) & 0x1ffu);
    uint32_t pd_i = (uint32_t)((virt >> 21) & 0x1ffu);
    uint32_t pt_i = (uint32_t)((virt >> 12) & 0x1ffu);
    uint64_t pml4e = kernel_pml4[pml4_i];
    if (!(pml4e & VM_FLAG_PRESENT)) return 0;
    PageTable* pdpt = (PageTable*)(pml4e & ~0xfffull);
    uint64_t pdpte = (*pdpt)[pdpt_i];
    if (!(pdpte & VM_FLAG_PRESENT)) return 0;
    PageTable* pd = (PageTable*)(pdpte & ~0xfffull);
    uint64_t pde = (*pd)[pd_i];
    if (!(pde & VM_FLAG_PRESENT)) return 0;
    if (pde & VM_FLAG_HUGE)
        return (pde & ~0x1fffffull) | (virt & 0x1fffffull);
    PageTable* pt = (PageTable*)(pde & ~0xfffull);
    uint64_t pte = (*pt)[pt_i];
    if (!(pte & VM_FLAG_PRESENT)) return 0;
    return (pte & ~0xfffull) | (virt & 0xfffull);
}
