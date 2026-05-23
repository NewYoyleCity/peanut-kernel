/* idt.c -- Interrupt Descriptor Table setup for x86-64.
 *
 * Initialises a full 256-entry IDT.  All entries default to a handler that
 * panics; specific entries are overridden for CPU exceptions (#UD, #GP, #PF)
 * and for IRQ 0 (timer, hooked later by idt_set_handler).
 *
 * Design decisions:
 *   - KERNEL_CODE_SELECTOR is 0x08 (GDT entry 1, ring 0 code).
 *   - Interrupt-gate attributes (0x8E) disable further interrupts in the
 *     handler, which is appropriate for exception handlers.
 *   - idt_set_handler() is a public API used by the scheduler to wire
 *     the PIT (IRQ 0 → vector 32) after the PIC has been remapped. */

#include "cpu/idt.h"
#include "freelib/kpanic.h"
#include "freelib/kstdio.h"
#include "freelib/kstdint.h"

#define IDT_ENTRIES 256
#define KERNEL_CODE_SELECTOR 0x08
#define IDT_INTERRUPT_GATE 0x8E

struct IDTEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct IDTDescriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct IDTEntry idt[IDT_ENTRIES];
static struct IDTDescriptor idt_ptr;

extern void interrupt_default_entry(void);
extern void isr_invalid_opcode(void);
extern void isr_general_protection(void);
extern void isr_page_fault(void);


/* idt_set_gate -- write an interrupt-gate descriptor into the IDT.
 */static void idt_set_gate(uint8_t vector, void (*handler)(void)) {
    uint64_t offset = (uint64_t)handler;

    idt[vector].offset_low = offset & 0xFFFF;
    idt[vector].selector = KERNEL_CODE_SELECTOR;
    idt[vector].ist = 0;
    idt[vector].attributes = IDT_INTERRUPT_GATE;
    idt[vector].offset_mid = (offset >> 16) & 0xFFFF;
    idt[vector].offset_high = (offset >> 32) & 0xFFFFFFFF;
    idt[vector].zero = 0;
}


/* idt_init -- fill 256 IDT entries with default handler; set exception gates; load IDTR.
 */
void idt_init(void) {
    for (uint16_t vector = 0; vector < IDT_ENTRIES; vector++) {
        idt_set_gate((uint8_t)vector, interrupt_default_entry);
    }
    idt_set_gate(6, isr_invalid_opcode);
    idt_set_gate(13, isr_general_protection);
    idt_set_gate(14, isr_page_fault);

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)idt;

    __asm__ volatile("lidt %0" : : "m"(idt_ptr) : "memory");
}


/* idt_set_handler -- override an IDT entry at a given vector (used for IRQ wiring).
 */
void idt_set_handler(uint8_t vector, void (*handler)(void)) {
    idt_set_gate(vector, handler);
}


/* interrupt_default_handler -- unhandled interrupt / exception: panic.
 */
void interrupt_default_handler(void) {
    kpanic("[Peanut kernel - panic - Unhandled CPU exception or interrupt]");
}


/* cpu_exception_handler -- log CPU exception details and panic.
 */
void cpu_exception_handler(uint64_t vector, uint64_t error, uint64_t rip) {
    kprint("CPU exception vector ");
    kprint_int(vector);
    kprint(" error ");
    kprint_hex(error);
    kprint(" rip ");
    kprint_hex(rip);
    kprint("\n");
    kpanic("[Peanut kernel - panic - CPU exception]");
}
