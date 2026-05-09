#include "drivers/entropy.h"

static uint64_t entropy_state = 0xD1B54A32D192ED03ull;
static int has_rdrand;
static int has_rdseed;

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

void entropy_mix(uint64_t value) {
    entropy_state ^= value + 0x9E3779B97F4A7C15ull + (entropy_state << 6) + (entropy_state >> 2);
    entropy_state ^= entropy_state >> 30;
    entropy_state *= 0xBF58476D1CE4E5B9ull;
    entropy_state ^= entropy_state >> 27;
    entropy_state *= 0x94D049BB133111EBull;
    entropy_state ^= entropy_state >> 31;
}

void entropy_init(void) {
    uint32_t a, b, c, d;
    cpuid(1, 0, &a, &b, &c, &d);
    has_rdrand = (c & (1u << 30)) != 0;
    cpuid(7, 0, &a, &b, &c, &d);
    has_rdseed = (b & (1u << 18)) != 0;
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    entropy_mix(((uint64_t)hi << 32) | lo);
}

int entropy_has_hardware_rng(void) {
    return has_rdrand || has_rdseed;
}

uint8_t entropy_random_byte(void) {
    uint64_t x = 0;
    if (has_rdseed) {
        for (uint32_t i = 0; i < 64; i++) {
            if (rdseed64(&x)) {
                entropy_mix(x);
                return (uint8_t)x;
            }
        }
    }
    if (has_rdrand) {
        for (uint32_t i = 0; i < 16; i++) {
            if (rdrand64(&x)) {
                entropy_mix(x);
                return (uint8_t)x;
            }
        }
    }
    __asm__ volatile("rdtsc" : "=a"(((uint32_t*)&x)[0]), "=d"(((uint32_t*)&x)[1]));
    entropy_mix(x);
    return (uint8_t)(entropy_state >> 56);
}

uint8_t entropy_urandom_byte(void) {
    uint64_t x = 0;
    if (has_rdrand && rdrand64(&x))
        entropy_mix(x);
    else {
        uint32_t lo, hi;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        entropy_mix(((uint64_t)hi << 32) | lo);
    }
    entropy_state ^= entropy_state << 13;
    entropy_state ^= entropy_state >> 7;
    entropy_state ^= entropy_state << 17;
    return (uint8_t)(entropy_state >> 56);
}
