#ifndef PSF_FONT_H
#define PSF_FONT_H

#include "freelib/kstdint.h"

typedef struct {
    const uint8_t* glyphs;
    uint32_t glyph_count;
    uint32_t glyph_width;
    uint32_t glyph_height;
    uint32_t bytes_per_glyph;
} PsfFont;

int psf_terminal_font(PsfFont* out);

#endif
