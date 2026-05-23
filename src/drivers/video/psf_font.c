/* psf_font.c -- PC Screen Font (PSF) loader.
 *
 * Reads an embedded PSFv1/PSFv2 font from a linker symbol and fills
 * a PsfFont struct for use by the framebuffer console.
 */

#include "drivers/video/psf_font.h"

extern const uint8_t _binary_src_fonts_terminal_psf_start[];
extern const uint8_t _binary_src_fonts_terminal_psf_end[];

int psf_terminal_font(PsfFont* out) {
    const uint8_t* p = _binary_src_fonts_terminal_psf_start;
    const uint8_t* end = _binary_src_fonts_terminal_psf_end;
    if (!out || end <= p + 4)
        return -1;
    if (p[0] == 0x36 && p[1] == 0x04) {
        uint32_t mode = p[2];
        uint32_t char_size = p[3];
        out->glyph_count = (mode & 0x01) ? 512 : 256;
        out->glyph_width = 8;
        out->glyph_height = char_size;
        out->bytes_per_glyph = char_size;
        out->glyphs = p + 4;
        if (out->glyphs + out->glyph_count * out->bytes_per_glyph > end)
            return -1;
        return 0;
    }
    return -1;
}
