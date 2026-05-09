#include "freelib/kstdint.h"
#include "drivers/input/ps2.h"

#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_CMD_PORT 0x64

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static int ps2_data_ready(void) {
    return (inb(PS2_STATUS_PORT) & 1) != 0;
}

static int ps2_output_is_mouse(void) {
    return (inb(PS2_STATUS_PORT) & 0x20) != 0;
}

static void ps2_wait_input_clear(void) {
    for (uint32_t i = 0; i < 100000 && (inb(PS2_STATUS_PORT) & 2); i++) {}
}

static char scancode_to_char(uint8_t scancode) {
    static const char map[] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
    };
    if (scancode & 0x80)
        return 0;
    if (scancode < sizeof(map))
        return map[scancode];
    return 0;
}

void ps2_init(void) {
    while (ps2_data_ready())
        (void)inb(PS2_DATA_PORT);

    ps2_wait_input_clear();
    outb(PS2_CMD_PORT, 0xA8);
    ps2_wait_input_clear();
    outb(PS2_CMD_PORT, 0xD4);
    ps2_wait_input_clear();
    outb(PS2_DATA_PORT, 0xF4);
    for (uint32_t i = 0; i < 100000 && !ps2_data_ready(); i++) {}
    if (ps2_data_ready())
        (void)inb(PS2_DATA_PORT);
}

int ps2_poll_char(char* out) {
    if (!out || !ps2_data_ready() || ps2_output_is_mouse())
        return 0;

    uint8_t scancode = inb(PS2_DATA_PORT);
    char c = scancode_to_char(scancode);
    if (!c)
        return 0;
    *out = c;
    return 1;
}

char ps2_get_char(void) {
    char c = 0;
    while (!ps2_poll_char(&c)) {}
    return c;
}

int ps2_poll_mouse_packet(uint8_t packet[3]) {
    static uint8_t bytes[3];
    static uint8_t pos;
    if (!packet || !ps2_data_ready() || !ps2_output_is_mouse())
        return 0;
    uint8_t b = inb(PS2_DATA_PORT);
    if (pos == 0 && (b & 0x08) == 0)
        return 0;
    bytes[pos++] = b;
    if (pos < 3)
        return 0;
    packet[0] = bytes[0];
    packet[1] = bytes[1];
    packet[2] = bytes[2];
    pos = 0;
    return 1;
}
