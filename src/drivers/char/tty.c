#include "drivers/char/tty.h"
#include "drivers/bus/io.h"
#include "freelib/kstdio.h"

#define COM1 0x3F8
#define UART_RBR 0
#define UART_THR 0
#define UART_IER 1
#define UART_IIR 2
#define UART_FCR 2
#define UART_LCR 3
#define UART_MCR 4
#define UART_LSR 5

#define LSR_DR 0x01
#define LSR_THRE 0x20

#define TTY_RBUF_SIZE 256

static int tty_initialized;
static uint8_t rbuf[TTY_RBUF_SIZE];
static uint32_t rbuf_head;
static uint32_t rbuf_tail;

static uint8_t tty_inb(uint16_t reg) {
    return inb(COM1 + reg);
}

static void tty_outb(uint16_t reg, uint8_t val) {
    outb(COM1 + reg, val);
}

static void tty_irq_handler(void) {
    while (tty_inb(UART_LSR) & LSR_DR) {
        uint8_t c = tty_inb(UART_RBR);
        uint32_t next = (rbuf_head + 1) % TTY_RBUF_SIZE;
        if (next != rbuf_tail) {
            rbuf[rbuf_head] = c;
            rbuf_head = next;
        }
    }
}

void tty_init(void) {
    if (tty_initialized) return;
    tty_outb(UART_IER, 0x00);
    tty_outb(UART_LCR, 0x80);
    tty_outb(UART_RBR, 0x01);
    tty_outb(UART_IER, 0x00);
    tty_outb(UART_LCR, 0x03);
    tty_outb(UART_FCR, 0xC7);
    tty_outb(UART_MCR, 0x0B);
    tty_outb(UART_IER, 0x01);
    tty_initialized = 1;
    for (uint32_t i = 0; i < TTY_RBUF_SIZE; i++) rbuf[i] = 0;
    rbuf_head = 0;
    rbuf_tail = 0;
    kprint("TTY: COM1 initialized\n");
}

int tty_read_char(char* out) {
    if (!tty_initialized) return -1;
    if (rbuf_head == rbuf_tail) {
        if (tty_inb(UART_LSR) & LSR_DR)
            tty_irq_handler();
        if (rbuf_head == rbuf_tail)
            return -1;
    }
    *out = (char)rbuf[rbuf_tail];
    rbuf_tail = (rbuf_tail + 1) % TTY_RBUF_SIZE;
    return 0;
}

int tty_write_char(char c) {
    if (!tty_initialized) return -1;
    for (uint32_t i = 0; i < 100000 && !(tty_inb(UART_LSR) & LSR_THRE); i++) {}
    tty_outb(UART_THR, (uint8_t)c);
    return 0;
}

int tty_can_read(void) {
    if (!tty_initialized) return 0;
    if (rbuf_head != rbuf_tail) return 1;
    if (tty_inb(UART_LSR) & LSR_DR) {
        tty_irq_handler();
        return (rbuf_head != rbuf_tail) ? 1 : 0;
    }
    return 0;
}
