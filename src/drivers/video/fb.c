#include "drivers/video/fb.h"
#include "drivers/video/psf_font.h"
#include "drivers/bus/io.h"

#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER 8

#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA  0x01CF
#define VBE_DISPI_INDEX_ID      0
#define VBE_DISPI_INDEX_XRES    1
#define VBE_DISPI_INDEX_YRES    2
#define VBE_DISPI_INDEX_BPP     3
#define VBE_DISPI_INDEX_ENABLE  4
#define VBE_DISPI_DISABLED      0x00
#define VBE_DISPI_ENABLED       0x01
#define VBE_DISPI_LFB_ENABLED   0x40
#define VBE_DISPI_NOCLEARMEM    0x80
#define VBE_LFB_PHYS_ADDR       0xE0000000ull

typedef struct {
    uint32_t type;
    uint32_t size;
} MbTag;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t fb_type;
    uint16_t reserved;
} MbFramebufferTag;

static uint32_t* fb;
static uint32_t pitch_pixels;
static uint32_t width;
static uint32_t height;
static uint32_t cursor_x;
static uint32_t cursor_y;
static PsfFont font;

static void vbe_write(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

int fb_init_direct(void) {
    if (psf_terminal_font(&font) != 0)
        return -1;
    vbe_write(VBE_DISPI_INDEX_ID, 0xB0C4);
    vbe_write(VBE_DISPI_INDEX_XRES, 1024);
    vbe_write(VBE_DISPI_INDEX_YRES, 768);
    vbe_write(VBE_DISPI_INDEX_BPP, 32);
    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
    fb = (uint32_t*)VBE_LFB_PHYS_ADDR;
    pitch_pixels = 1024;
    width = 1024;
    height = 768;
    fb_clear();
    return 0;
}

static uint32_t align8(uint32_t v) {
    return (v + 7u) & ~7u;
}

void fb_init_from_multiboot(uint64_t mbi) {
    if (!mbi || psf_terminal_font(&font) != 0)
        return;
    uint8_t* p = (uint8_t*)(uintptr_t)mbi;
    uint32_t total = *(uint32_t*)p;
    uint32_t off = 8;
    while (off + sizeof(MbTag) <= total) {
        MbTag* tag = (MbTag*)(p + off);
        if (tag->type == 0)
            break;
        if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER && tag->size >= sizeof(MbFramebufferTag)) {
            MbFramebufferTag* t = (MbFramebufferTag*)tag;
            if (t->addr && t->pitch && t->width && t->height && t->bpp == 32 && (t->fb_type == 1 || t->fb_type == 2)) {
                fb = (uint32_t*)(uintptr_t)t->addr;
                pitch_pixels = t->pitch / 4u;
                width = t->width;
                height = t->height;
                fb_clear();
                return;
            }
        }
        off += align8(tag->size);
    }
}

int fb_ready(void) {
    return fb != 0;
}

static void scroll_if_needed(void) {
    uint32_t glyph_h = font.glyph_height;
    uint32_t rows = height / glyph_h;
    if (cursor_y < rows)
        return;
    uint32_t row_px = glyph_h;
    for (uint32_t y = row_px; y < height; y++) {
        for (uint32_t x = 0; x < width; x++)
            fb[(y - row_px) * pitch_pixels + x] = fb[y * pitch_pixels + x];
    }
    for (uint32_t y = height - row_px; y < height; y++) {
        for (uint32_t x = 0; x < width; x++)
            fb[y * pitch_pixels + x] = 0x00000000u;
    }
    cursor_y = rows - 1;
}

void fb_clear(void) {
    if (!fb)
        return;
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++)
            fb[y * pitch_pixels + x] = 0x00000000u;
    }
    cursor_x = 0;
    cursor_y = 0;
}

void fb_putc(char c) {
    if (!fb)
        return;
    uint32_t cols = width / font.glyph_width;
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        scroll_if_needed();
        return;
    }
    if ((uint8_t)c >= font.glyph_count)
        c = '?';
    const uint8_t* glyph = font.glyphs + (uint8_t)c * font.bytes_per_glyph;
    uint32_t px = cursor_x * font.glyph_width;
    uint32_t py = cursor_y * font.glyph_height;
    for (uint32_t row = 0; row < font.glyph_height; row++) {
        uint8_t bits = glyph[row];
        for (uint32_t col = 0; col < font.glyph_width; col++) {
            uint32_t color = (bits & (0x80u >> col)) ? 0x00E8E8E8u : 0x00000000u;
            if (px + col < width && py + row < height)
                fb[(py + row) * pitch_pixels + px + col] = color;
        }
    }
    cursor_x++;
    if (cursor_x >= cols) {
        cursor_x = 0;
        cursor_y++;
        scroll_if_needed();
    }
}
