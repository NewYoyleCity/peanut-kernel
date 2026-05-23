#include "drivers/entropy.h"
#include "drivers/bus/io.h"
#include "drivers/bus/pci.h"

static uint64_t entropy_state = 0xD1B54A32D192ED03ull;
static int has_rdrand;
static int has_rdseed;
static uint64_t pool[16];
static uint32_t pool_idx;

static void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(subleaf));
}

static int rdrand64(uint64_t* out) {
    uint8_t ok;
    __asm__ volatile("rdrand %0; setc %1" : "=r"(*out), "=qm"(ok));
    return ok;
}

static int rdseed64(uint64_t* out) {
    uint8_t ok;
    __asm__ volatile("rdseed %0; setc %1" : "=r"(*out), "=qm"(ok));
    return ok;
}

static uint64_t read_tsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t read_pmc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdpmc" : "=a"(lo), "=d"(hi) : "c"(0));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t read_flags(void) {
    uint64_t r;
    __asm__ volatile("pushfq; pop %0" : "=r"(r));
    return r;
}

static uint64_t read_bp(void) {
    uint64_t r;
    __asm__ volatile("mov %%rbp, %0" : "=r"(r));
    return r;
}

void entropy_mix(uint64_t value) {
    pool[pool_idx & 15] ^= value;
    pool_idx++;
    entropy_state ^= value + 0x9E3779B97F4A7C15ull + (entropy_state << 6) + (entropy_state >> 2);
    entropy_state ^= entropy_state >> 30;
    entropy_state *= 0xBF58476D1CE4E5B9ull;
    entropy_state ^= entropy_state >> 27;
    entropy_state *= 0x94D049BB133111EBull;
    entropy_state ^= entropy_state >> 31;
    for (int i = 0; i < 4; i++)
        entropy_state ^= pool[(pool_idx - 1 - i) & 15] << (i * 13);
}

static void sample_timing_jitter(void) {
    for (int i = 0; i < 32; i++) {
        uint64_t a = read_tsc();
        uint64_t b = read_tsc();
        uint64_t c = read_tsc();
        entropy_mix((b - a) ^ (c - b) ^ (uint64_t)i);
    }
}

static void harvest_pci_entropy(void) {
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            PciAddress addr;
            addr.bus = (uint8_t)bus;
            addr.slot = (uint8_t)slot;
            addr.function = 0;
            uint32_t vendor = pci_read32(addr, 0);
            if ((vendor & 0xFFFF) == 0xFFFF) continue;
            entropy_mix(vendor);
            uint32_t class_rev = pci_read32(addr, 8);
            entropy_mix(class_rev);
            uint32_t bar0 = pci_read32(addr, 0x10);
            if (bar0) entropy_mix(bar0);
        }
    }
}

void entropy_init(void) {
    uint32_t a, b, c, d;
    cpuid(1, 0, &a, &b, &c, &d);
    has_rdrand = (c & (1u << 30)) != 0;
    cpuid(7, 0, &a, &b, &c, &d);
    has_rdseed = (b & (1u << 18)) != 0;
    entropy_mix(read_tsc());
    entropy_mix(read_bp());
    entropy_mix(read_flags());
    entropy_mix((uint64_t)&a);
    entropy_mix(read_pmc());
    sample_timing_jitter();
    harvest_pci_entropy();
    for (int i = 0; i < 16; i++) {
        uint64_t x = 0;
        if (has_rdseed) rdseed64(&x);
        else if (has_rdrand) rdrand64(&x);
        pool[i] = x ^ read_tsc();
    }
    pool_idx = 0;
    sample_timing_jitter();
}

int entropy_has_hardware_rng(void) {
    return has_rdrand || has_rdseed;
}

static uint64_t entropy_gather_raw(void) {
    uint64_t x = 0;
    if (has_rdseed) {
        for (uint32_t i = 0; i < 64; i++)
            if (rdseed64(&x)) return x;
    }
    if (has_rdrand) {
        for (uint32_t i = 0; i < 16; i++)
            if (rdrand64(&x)) return x;
    }
    x = read_tsc() ^ read_pmc() ^ read_flags() ^ read_bp();
    return x;
}

uint8_t entropy_random_byte(void) {
    uint64_t x = entropy_gather_raw();
    entropy_mix(x);
    entropy_mix(read_tsc());
    return (uint8_t)(entropy_state >> 56) ^ (uint8_t)(entropy_state >> 24);
}

uint8_t entropy_urandom_byte(void) {
    uint64_t x = 0;
    if (has_rdrand && rdrand64(&x))
        entropy_mix(x);
    else
        entropy_mix(read_tsc() ^ read_flags());
    entropy_state ^= entropy_state << 13;
    entropy_state ^= entropy_state >> 7;
    entropy_state ^= entropy_state << 17;
    entropy_state ^= pool[pool_idx & 15];
    pool_idx++;
    return (uint8_t)(entropy_state >> 56) ^ (uint8_t)(entropy_state);
}
