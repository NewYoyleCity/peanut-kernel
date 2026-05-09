#include "freelib/kstdint.h"

struct GDTEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct GDTDescriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct GDT {
    struct GDTEntry null;
    struct GDTEntry k_code;
    struct GDTEntry k_data;
    struct GDTEntry u_data;
    struct GDTEntry u_code;
    uint64_t tss_low;
    uint64_t tss_high;
} __attribute__((packed)) __attribute__((aligned(0x1000)));

static struct GDT gdt;
static struct GDTDescriptor gdt_ptr;

void gdt_set_entry(struct GDTEntry* entry, uint8_t access, uint8_t gran) {
    entry->limit_low = 0;
    entry->base_low = 0;
    entry->base_middle = 0;
    entry->access = access;
    entry->granularity = gran;
    entry->base_high = 0;
}

void gdt_init_for_user() {
    
    gdt_set_entry(&gdt.null, 0, 0);
    gdt_set_entry(&gdt.k_code, 0x9A, 0x20); 
    gdt_set_entry(&gdt.k_data, 0x92, 0x00); 
    gdt_set_entry(&gdt.u_data, 0xF2, 0x00); 
    gdt_set_entry(&gdt.u_code, 0xFA, 0x20); 

    gdt_ptr.limit = sizeof(struct GDT) - 1;
    gdt_ptr.base = (uint64_t)&gdt;

    __asm__ volatile("lgdt %0" : : "m"(gdt_ptr));
}

struct TSSEntry {
    uint32_t reserved0;
    uint64_t rsp0;      
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint32_t reserved2;
    uint32_t reserved3;
    uint16_t reserved4;
    uint16_t iopb_offset;
} __attribute__((packed));

struct TSSEntry kernel_tss;
uint8_t kernel_stack[8192]; 

static void gdt_set_tss(uint64_t base, uint32_t limit) {
    gdt.tss_low = ((uint64_t)(limit & 0xFFFF)) |
                  ((uint64_t)(base & 0xFFFFFF) << 16) |
                  ((uint64_t)0x89 << 40) |
                  ((uint64_t)((limit >> 16) & 0xF) << 48) |
                  ((uint64_t)((base >> 24) & 0xFF) << 56);
    gdt.tss_high = base >> 32;
}

void tss_init() {
    
    kernel_tss.rsp0 = (uint64_t)kernel_stack + sizeof(kernel_stack);
    kernel_tss.iopb_offset = sizeof(struct TSSEntry);

    gdt_set_tss((uint64_t)&kernel_tss, sizeof(struct TSSEntry) - 1);
    __asm__ volatile("ltr %%ax" : : "a"((uint16_t)0x28) : "memory");
}
