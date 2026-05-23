/* io.h -- x86 I/O port access inline functions.
 *
 * Provides inb/inw/inl/outb/outw/outl for port-mapped I/O,
 * plus rep insw/outsw for block transfers and io_wait().
 */

#ifndef IO_H
#define IO_H

#include "freelib/kstdint.h"

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void io_wait() {
    outb(0x80, 0);
}

static inline void insw(uint16_t port, void* buffer, uint32_t words) {
    __asm__ volatile("rep insw" : "+D"(buffer), "+c"(words) : "d"(port) : "memory");
}

static inline void outsw(uint16_t port, const void* buffer, uint32_t words) {
    __asm__ volatile("rep outsw" : "+S"(buffer), "+c"(words) : "d"(port));
}

#endif
