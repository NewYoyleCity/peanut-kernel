#include "kstdio.h"
#include "config.h"
#include "drivers/bus/io.h"
#include "drivers/video/fb.h"
#include "cpu/pit.h"

static int cursor_x = 0;
static int cursor_y = 0;
static int serial_ready = 0;
const int SCREEN_WIDTH = 80;
const int SCREEN_HEIGHT = 25;

static void serial_init() {
    if (serial_ready) return;

    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);

    serial_ready = 1;
}

static void serial_putc(char c) {
    serial_init();
    for (uint32_t i = 0; i < 100000 && !(inb(0x3F8 + 5) & 0x20); i++) {}
    outb(0x3F8, (uint8_t)c);
}

void kputc(char c) {
    unsigned short* vga_buffer = (unsigned short*)0xB8000;

    if (c == '\n') serial_putc('\r');
    serial_putc(c);

    if (fb_ready()) {
        fb_putc(c);
        return;
    }

    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else {
        if (cursor_x >= SCREEN_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
        if (cursor_x < SCREEN_WIDTH) {
            int index = (cursor_y * SCREEN_WIDTH) + cursor_x;
            vga_buffer[index] = (unsigned short)c | (CONFIG_VGA_COLOR << 8);
        }
        cursor_x++;
    }

    
    if (cursor_x > SCREEN_WIDTH) {
        cursor_x = SCREEN_WIDTH;
    }

    
    if (cursor_y >= SCREEN_HEIGHT) {
        kclear(); 
    }
}

void kprint(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        kputc(str[i]);
    }
}

void kprint_hex(uint64_t n) {
    char* chars = "0123456789ABCDEF";
    kprint("0x");
    
    for (int i = 60; i >= 0; i -= 4) {
        kputc(chars[(n >> i) & 0xF]);
    }
}

void kprint_int(int64_t n) {
    if (n == 0) {
        kputc('0');
        return;
    }
    uint64_t abs_n;
    if (n < 0) {
        kputc('-');
        abs_n = (uint64_t)(-(n + 1)) + 1u;
    } else {
        abs_n = (uint64_t)n;
    }
    char buffer[21];
    int i = 0;
    while (abs_n > 0) {
        buffer[i++] = (char)((abs_n % 10u) + '0');
        abs_n /= 10u;
    }
    while (i > 0) {
        kputc(buffer[--i]);
    }
}

void kclear() {
    if (fb_ready()) {
        fb_clear();
    }
    unsigned short* vga_buffer = (unsigned short*)0xB8000;
    unsigned short blank = (unsigned short)' ' | (CONFIG_VGA_COLOR << 8);
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        vga_buffer[i] = blank;
    }
    cursor_x = 0;
    cursor_y = 0;
}

static void print_padded(uint64_t n, int digits) {
    char buf[12];
    int i = digits;
    buf[i] = '\0';
    while (i > 0) {
        i--;
        buf[i] = (char)((n % 10u) + '0');
        n /= 10u;
    }
    kprint(buf);
}

void kprint_timed(const char* str) {
    uint64_t ms = pit_uptime_ms();
    uint64_t sec = ms / 1000u;
    uint64_t msec = ms % 1000u;
    kputc('[');
    print_padded(sec / 60u, 2);
    kputc(':');
    print_padded(sec % 60u, 2);
    kputc('.');
    print_padded(msec, 3);
    kprint("] ");
    kprint(str);
}

void kprint_timed_hex(const char* label, uint64_t n) {
    kprint_timed(label);
    kprint_hex(n);
    kputc('\n');
}

void kprint_timed_int(const char* label, int64_t n) {
    kprint_timed(label);
    kprint_int(n);
    kputc('\n');
}
