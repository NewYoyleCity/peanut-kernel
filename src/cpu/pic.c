/* pic.c -- 8259A Programmable Interrupt Controller driver.
 *
 * Remaps the PIC from the BIOS-default vectors (0-15) to vectors 32-47 so
 * that hardware interrupts do not collide with x86 CPU exceptions (0-31).
 * After remapping, all interrupt lines except the PIT timer (IRQ 0) are
 * masked off.
 *
 * Design note: on modern systems the PIC is often replaced by the APIC,
 * but this kernel retains the legacy PIC for simplicity in the early boot
 * path.  The remap follows the classic ICW1-ICW4 sequence. */

#include "cpu/pic.h"
#include "drivers/bus/io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_ICW4 0x01
#define ICW1_INIT 0x10
#define ICW4_8086 0x01


/* pic_init -- remap PIC to vectors 32-47 and mask all IRQs except timer.
 */
void pic_init(void) {
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC1_DATA, 32);
    outb(PIC2_DATA, 40);
    outb(PIC1_DATA, 4);
    outb(PIC2_DATA, 2);
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);
    outb(PIC1_DATA, 0xFE);
    outb(PIC2_DATA, 0xFF);
}


/* pic_master_eoi -- send non-specific EOI to the master PIC.
 */
void pic_master_eoi(void) {
    outb(PIC1_CMD, 0x20);
}
