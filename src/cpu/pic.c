#include "cpu/pic.h"
#include "drivers/bus/io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_ICW4 0x01
#define ICW1_INIT 0x10
#define ICW4_8086 0x01

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

void pic_master_eoi(void) {
    outb(PIC1_CMD, 0x20);
}
