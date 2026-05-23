/* Decompressor entry point - called from decompress_entry.asm */
/* The inflate implementation is in lib/inflate.c */
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

/* Minimal inflate implementation based on public domain puff.c */
#define MAX_BITS 15

struct BitState {
    const unsigned char* in;
    uint32_t in_off;
    uint32_t in_len;
    uint32_t bitbuf;
    uint32_t bitcnt;
};

static int bits_needed(struct BitState* s, uint32_t n) {
    while (s->bitcnt < n) {
        if (s->in_off >= s->in_len) return -1;
        s->bitbuf |= (uint32_t)s->in[s->in_off++] << s->bitcnt;
        s->bitcnt += 8;
    }
    return 0;
}

static uint32_t bits_get(struct BitState* s, uint32_t n) {
    uint32_t val = s->bitbuf & ((1u << n) - 1);
    s->bitbuf >>= n;
    s->bitcnt -= n;
    return val;
}

static void bits_align(struct BitState* s) {
    s->bitcnt = 0;
    s->bitbuf = 0;
}

static int decode_symbol(struct BitState* s, const unsigned short* table, int n) {
    int index = 0;
    int first = 0;
    int count = n;
    int len = 0;
    uint32_t code = 0;
    while (1) {
        if (bits_needed(s, 1) < 0) return -1;
        code = (code << 1) | bits_get(s, 1);
        len++;
        int next = first + count;
        int found = 0;
        while (index < next) {
            if ((table[index] & 0xFF) == (unsigned char)len && (table[index] >> 8) == (unsigned short)code) {
                found = 1;
                break;
            }
            index++;
        }
        if (found) return table[index + 1];
        first += count;
        count = 0;
        for (int i = index; i < n * 2; i += 2) {
            if ((table[i] & 0xFF) > (unsigned char)len) count++;
        }
    }
}

static int copy_match(unsigned char* out, uint32_t* out_pos, uint32_t out_len, int length, int distance) {
    if (*out_pos + (uint32_t)length > out_len) return -1;
    int d = distance;
    if (d > (int)*out_pos) d = (int)*out_pos;
    unsigned char* src = out + *out_pos - d;
    unsigned char* dst = out + *out_pos;
    for (int i = 0; i < length; i++) dst[i] = src[i];
    *out_pos += length;
    return 0;
}

static const int len_base[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const int len_extra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const int dist_base[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const int dist_extra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int inflate_block(struct BitState* s, unsigned char* out, uint32_t* out_pos, uint32_t out_len,
                         const unsigned short* len_table, int len_n,
                         const unsigned short* dist_table, int dist_n) {
    while (1) {
        int sym = decode_symbol(s, len_table, len_n);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (*out_pos >= out_len) return -1;
            out[(*out_pos)++] = (unsigned char)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            int idx = sym - 257;
            int length = len_base[idx];
            int extra = len_extra[idx];
            if (extra > 0) {
                if (bits_needed(s, extra) < 0) return -1;
                length += (int)bits_get(s, extra);
            }
            int dsym = decode_symbol(s, dist_table, dist_n);
            if (dsym < 0) return -1;
            int distance = dist_base[dsym];
            extra = dist_extra[dsym];
            if (extra > 0) {
                if (bits_needed(s, extra) < 0) return -1;
                distance += (int)bits_get(s, extra);
            }
            if (copy_match(out, out_pos, out_len, length, distance) < 0) return -1;
        }
    }
}

/* Fixed Huffman tables */
static int build_fixed_len(unsigned short table[288*2]) {
    int idx = 0;
    for (int i = 0; i < 144; i++) { table[idx++] = (unsigned short)((8 << 8) | 8); table[idx++] = (unsigned short)i; }
    for (int i = 144; i < 256; i++) { table[idx++] = (unsigned short)((9 << 8) | 9); table[idx++] = (unsigned short)i; }
    for (int i = 256; i < 280; i++) { table[idx++] = (unsigned short)((7 << 8) | 7); table[idx++] = (unsigned short)i; }
    for (int i = 280; i < 288; i++) { table[idx++] = (unsigned short)((8 << 8) | 8); table[idx++] = (unsigned short)i; }
    return 288;
}

static int build_fixed_dist(unsigned short table[32*2]) {
    int idx = 0;
    for (int i = 0; i < 32; i++) { table[idx++] = (unsigned short)((5 << 8) | 5); table[idx++] = (unsigned short)i; }
    return 32;
}

/* Dynamic Huffman table building */
static int read_huff_table(struct BitState* s, const unsigned char* code_lens, int n, unsigned short* table) {
    int max_len = 0;
    for (int i = 0; i < n; i++)
        if (code_lens[i] > max_len) max_len = code_lens[i];
    if (max_len > MAX_BITS) return -1;
    int bl_count[17] = {0};
    for (int i = 0; i < n; i++)
        if (code_lens[i] > 0) bl_count[code_lens[i]]++;
    int code = 0;
    int next_code[17] = {0};
    for (int bits = 1; bits <= max_len; bits++) {
        code = (code + bl_count[bits-1]) << 1;
        next_code[bits] = code;
    }
    int idx = 0;
    for (int i = 0; i < n; i++) {
        int len = code_lens[i];
        if (len == 0) continue;
        table[idx++] = (unsigned short)((len << 8) | (unsigned char)len);
        table[idx++] = (unsigned short)i;
    }
    return n;
}

static int inflate_dynamic(struct BitState* s, unsigned char* out, uint32_t* out_pos, uint32_t out_len,
                           unsigned short* len_table, unsigned short* dist_table) {
    if (bits_needed(s, 14) < 0) return -1;
    int nlen = 257 + (int)bits_get(s, 5);
    int ndist = 1 + (int)bits_get(s, 5);
    int ncode = 4 + (int)bits_get(s, 4);
    unsigned char lens[320];
    unsigned char code_order[19];
    static const int order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    for (int i = 0; i < 19; i++) code_order[i] = 0;
    for (int i = 0; i < ncode; i++) {
        if (bits_needed(s, 3) < 0) return -1;
        code_order[order[i]] = (unsigned char)bits_get(s, 3);
    }
    unsigned short code_table[19*2];
    int c_n = 19;
    int c_used = 0;
    int max_c = 0;
    for (int i = 0; i < 19; i++)
        if (code_order[i] > max_c) max_c = code_order[i];
    for (int b = 1; b <= max_c; b++) {
        for (int i = 0; i < 19; i++) {
            if (code_order[i] == b) {
                code_table[c_used++] = (unsigned short)((b << 8) | (unsigned char)b);
                code_table[c_used++] = (unsigned short)i;
            }
        }
    }
    int all_count = 0;
    while (all_count < nlen + ndist) {
        int sym = decode_symbol(s, code_table, c_n);
        if (sym < 0) return -1;
        if (sym < 16) {
            lens[all_count++] = (unsigned char)sym;
        } else if (sym == 16) {
            if (bits_needed(s, 2) < 0) return -1;
            int rep = 3 + (int)bits_get(s, 2);
            unsigned char val = all_count > 0 ? lens[all_count-1] : 0;
            for (int i = 0; i < rep && all_count < nlen + ndist; i++)
                lens[all_count++] = val;
        } else if (sym == 17) {
            if (bits_needed(s, 3) < 0) return -1;
            int rep = 3 + (int)bits_get(s, 3);
            for (int i = 0; i < rep && all_count < nlen + ndist; i++)
                lens[all_count++] = 0;
        } else if (sym == 18) {
            if (bits_needed(s, 7) < 0) return -1;
            int rep = 11 + (int)bits_get(s, 7);
            for (int i = 0; i < rep && all_count < nlen + ndist; i++)
                lens[all_count++] = 0;
        }
    }
    c_n = read_huff_table(s, lens, nlen, len_table);
    if (c_n < 0) return -1;
    c_n = read_huff_table(s, lens + nlen, ndist, dist_table);
    if (c_n < 0) return -1;
    return inflate_block(s, out, out_pos, out_len, len_table, nlen, dist_table, ndist);
}

uint64_t decompress_kernel(void* dst, const void* src, uint64_t dst_len, uint64_t src_len) {
    struct BitState s;
    s.in = (const unsigned char*)src;
    s.in_off = 0;
    s.in_len = (uint32_t)src_len;
    s.bitbuf = 0;
    s.bitcnt = 0;
    
    unsigned char* out = (unsigned char*)dst;
    uint32_t out_pos = 0;
    uint32_t out_len = (uint32_t)dst_len;
    
    unsigned short len_table[288*2];
    unsigned short dist_table[32*2];
    
    do {
        if (bits_needed(&s, 3) < 0) return 0;
        int bfinal = (int)bits_get(&s, 1);
        int btype = (int)bits_get(&s, 2);
        
        if (btype == 0) {
            bits_align(&s);
            if (s.in_off + 4 > s.in_len) return 0;
            uint32_t len = (uint32_t)s.in[s.in_off] | ((uint32_t)s.in[s.in_off+1] << 8);
            uint32_t nlen = (uint32_t)s.in[s.in_off+2] | ((uint32_t)s.in[s.in_off+3] << 8);
            s.in_off += 4;
            if (len != (~nlen & 0xFFFF)) return 0;
            if (out_pos + len > out_len) return 0;
            if (s.in_off + len > s.in_len) return 0;
            for (uint32_t i = 0; i < len; i++)
                out[out_pos++] = s.in[s.in_off++];
            if (bfinal) break;
        } else if (btype == 1) {
            int ln = build_fixed_len(len_table);
            int dn = build_fixed_dist(dist_table);
            if (inflate_block(&s, out, &out_pos, out_len, len_table, ln, dist_table, dn) < 0) return 0;
            if (bfinal) break;
        } else if (btype == 2) {
            if (inflate_dynamic(&s, out, &out_pos, out_len, len_table, dist_table) < 0) return 0;
            if (bfinal) break;
        } else {
            return 0;
        }
    } while (1);
    
    return (uint64_t)out_pos;
}
