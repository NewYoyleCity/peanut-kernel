/* inflate.c -- DEFLATE decompressor (RFC 1951).
 *
 * Minimal, standalone inflate implementation supporting stored, fixed
 * Huffman, and dynamic Huffman blocks.
 */

#include "lib/inflate.h"

struct BitState {
    const uint8_t* in;
    uint32_t in_off;
    uint32_t in_len;
    uint32_t bitbuf;
    uint32_t bitcnt;
};


/* bits_needed -- ensure at least n bits are available in the bit buffer.
 */static int bits_needed(struct BitState* s, uint32_t n) {
    while (s->bitcnt < n) {
        if (s->in_off >= s->in_len) return -1;
        s->bitbuf |= (uint32_t)s->in[s->in_off++] << s->bitcnt;
        s->bitcnt += 8;
    }
    return 0;
}


/* bits_get -- consume and return n bits from the bit buffer.
 */static uint32_t bits_get(struct BitState* s, uint32_t n) {
    uint32_t val = s->bitbuf & ((1u << n) - 1);
    s->bitbuf >>= n;
    s->bitcnt -= n;
    return val;
}


/* bits_skip_to_byte -- discard bits to align to a byte boundary.
 */static void bits_skip_to_byte(struct BitState* s) {
    uint32_t skip = s->bitcnt & 7;
    if (skip) {
        s->bitbuf >>= skip;
        s->bitcnt -= skip;
    }
}

struct HuffTable {
    uint16_t* count;
    uint16_t* symbol;
    uint16_t max_bits;
};


/* huff_build -- construct a canonical Huffman decoding table from code lengths.
 */static int huff_build(struct HuffTable* t, const uint16_t* lens, uint32_t num_syms) {
    uint32_t max = 0;
    for (uint32_t i = 0; i < num_syms; i++)
        if (lens[i] > max) max = lens[i];
    t->max_bits = (uint16_t)max;

    for (uint32_t i = 0; i <= max; i++)
        t->count[i] = 0;
    for (uint32_t i = 0; i < num_syms; i++)
        if (lens[i] > 0) t->count[lens[i]]++;

    uint32_t offs[16];
    offs[1] = 0;
    for (uint32_t i = 1; i < max; i++)
        offs[i + 1] = offs[i] + t->count[i];

    for (uint32_t i = 0; i < num_syms; i++) {
        if (lens[i] > 0)
            t->symbol[offs[lens[i]]++] = (uint16_t)i;
    }
    return 0;
}


/* huff_decode -- decode one symbol using the Huffman table.
 */static int huff_decode(struct BitState* s, struct HuffTable* t) {
    uint32_t code = 0;
    uint32_t first = 0;
    uint32_t index = 0;
    for (uint32_t len = 1; len <= t->max_bits; len++) {
        if (bits_needed(s, 1) != 0) return -1;
        code = (code << 1) | bits_get(s, 1);
        uint32_t c = t->count[len];
        if ((code - first) < c)
            return t->symbol[index + (code - first)];
        index += c;
        first = (first + c) << 1;
    }
    return -1;
}


/* decode_dist -- decode a distance value (base + extra bits).
 */static int decode_dist(struct BitState* s, struct HuffTable* dt) {
    int sym = huff_decode(s, dt);
    if (sym < 0) return -1;
    static const uint32_t extra[] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
        7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };
    static const uint32_t base[] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
        257,385,513,769,1025,1537,2049,3073,4097,6145,
        8193,12289,16385,24577
    };
    if ((uint32_t)sym >= 30) return -1;
    uint32_t d = base[sym];
    uint32_t e = extra[sym];
    if (e) {
        if (bits_needed(s, e) != 0) return -1;
        d += bits_get(s, e);
    }
    return (int)d;
}


/* inflate_block -- decompress one DEFLATE block (stored, fixed, or dynamic).
 */static int inflate_block(struct BitState* s, uint8_t* out, uint32_t out_len, uint32_t* written) {
    if (bits_needed(s, 3) != 0) return -1;
    uint32_t bfinal = bits_get(s, 1);
    uint32_t btype = bits_get(s, 2);

    if (btype == 0) {
        bits_skip_to_byte(s);
        if (s->in_off + 4 > s->in_len) return -1;
        uint32_t len = (uint32_t)s->in[s->in_off] | ((uint32_t)s->in[s->in_off + 1] << 8);
        uint32_t nlen = (uint32_t)s->in[s->in_off + 2] | ((uint32_t)s->in[s->in_off + 3] << 8);
        s->in_off += 4;
        if ((len ^ nlen) != 0xFFFFu) return -1;
        if (s->in_off + len > s->in_len) return -1;
        if (*written + len > out_len) return -1;
        for (uint32_t i = 0; i < len; i++)
            out[(*written)++] = s->in[s->in_off++];
        return bfinal ? 1 : 0;
    }

    if (btype == 1 || btype == 2) {
        uint16_t lit_count[16];
        uint16_t lit_sym[288];
        uint16_t dist_count[16];
        uint16_t dist_sym[32];
        struct HuffTable lit_tab = { lit_count, lit_sym, 0 };
        struct HuffTable dist_tab = { dist_count, dist_sym, 0 };

        if (btype == 1) {
            uint16_t lens[288];
            for (uint32_t i = 0; i < 144; i++) lens[i] = 8;
            for (uint32_t i = 144; i < 256; i++) lens[i] = 9;
            for (uint32_t i = 256; i < 280; i++) lens[i] = 7;
            for (uint32_t i = 280; i < 288; i++) lens[i] = 8;
            huff_build(&lit_tab, lens, 288);
            for (uint32_t i = 0; i < 32; i++) lens[i] = 5;
            huff_build(&dist_tab, lens, 32);
        } else {
            if (bits_needed(s, 14) != 0) return -1;
            uint32_t hlit = bits_get(s, 5) + 257;
            uint32_t hdist = bits_get(s, 5) + 1;
            uint32_t hclen = bits_get(s, 4) + 4;

            uint16_t cl_order[19] = {
                16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
            };
            uint16_t cl_lens[19];
            for (uint32_t i = 0; i < 19; i++) cl_lens[i] = 0;
            for (uint32_t i = 0; i < hclen; i++) {
                if (bits_needed(s, 3) != 0) return -1;
                cl_lens[cl_order[i]] = (uint16_t)bits_get(s, 3);
            }

            uint16_t cl_count[16];
            uint16_t cl_sym[19];
            struct HuffTable cl_tab = { cl_count, cl_sym, 0 };
            huff_build(&cl_tab, cl_lens, 19);

            uint32_t total = hlit + hdist;
            uint16_t all[320];
            uint32_t pos = 0;
            while (pos < total) {
                int sym = huff_decode(s, &cl_tab);
                if (sym < 0) return -1;
                if (sym < 16) {
                    all[pos++] = (uint16_t)sym;
                } else if (sym == 16) {
                    if (bits_needed(s, 2) != 0) return -1;
                    uint32_t rpt = bits_get(s, 2) + 3;
                    if (pos == 0) return -1;
                    uint16_t val = all[pos - 1];
                    for (uint32_t i = 0; i < rpt && pos < total; i++)
                        all[pos++] = val;
                } else if (sym == 17) {
                    if (bits_needed(s, 3) != 0) return -1;
                    uint32_t rpt = bits_get(s, 3) + 3;
                    for (uint32_t i = 0; i < rpt && pos < total; i++)
                        all[pos++] = 0;
                } else if (sym == 18) {
                    if (bits_needed(s, 7) != 0) return -1;
                    uint32_t rpt = bits_get(s, 7) + 11;
                    for (uint32_t i = 0; i < rpt && pos < total; i++)
                        all[pos++] = 0;
                }
            }
            huff_build(&lit_tab, all, hlit);
            huff_build(&dist_tab, all + hlit, hdist);
        }

        static const uint32_t len_extra[] = {
            0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
            3,3,3,3,4,4,4,4,5,5,5,5,0
        };
        static const uint32_t len_base[] = {
            3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
            35,43,51,59,67,83,99,115,131,163,195,227,258
        };

        for (;;) {
            int sym = huff_decode(s, &lit_tab);
            if (sym < 0) return -1;
            if ((uint32_t)sym < 256) {
                if (*written >= out_len) return -1;
                out[(*written)++] = (uint8_t)sym;
            } else if (sym == 256) {
                return bfinal ? 1 : 0;
            } else {
                uint32_t idx = (uint32_t)(sym - 257);
                if (idx > 28) return -1;
                uint32_t len = len_base[idx];
                uint32_t ext = len_extra[idx];
                if (ext) {
                    if (bits_needed(s, ext) != 0) return -1;
                    len += bits_get(s, ext);
                }
                int dist = decode_dist(s, &dist_tab);
                if (dist < 0) return -1;
                if ((uint32_t)dist > *written) return -1;
                uint8_t* src = out + *written - dist;
                for (uint32_t i = 0; i < len; i++) {
                    if (*written >= out_len) return -1;
                    out[*written] = src[i];
                    (*written)++;
                }
            }
        }
    }

    return -1;
}

uint32_t inflate(void* dst, uint32_t dst_len, const void* src, uint32_t src_len) {
    if (!dst || !src || dst_len == 0 || src_len == 0) return 0;
    struct BitState s;
    s.in = (const uint8_t*)src;
    s.in_off = 0;
    s.in_len = src_len;
    s.bitbuf = 0;
    s.bitcnt = 0;

    uint8_t* out = (uint8_t*)dst;
    uint32_t written = 0;

    for (;;) {
        int r = inflate_block(&s, out, dst_len, &written);
        if (r < 0) return 0;
        if (r == 1) return written;
    }
}
