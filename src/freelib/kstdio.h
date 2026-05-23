#ifndef KSTDIO_H
#define KSTDIO_H

#include "kstdint.h"

#ifdef __cplusplus
extern "C" {
#endif

void kclear();
void kputc(char c);
void kprint(const char* str);
void kprint_hex(uint64_t n);
void kprint_int(int64_t n);
void kprint_timed(const char* str);
void kprint_timed_hex(const char* label, uint64_t n);
void kprint_timed_int(const char* label, int64_t n);

#ifdef __cplusplus
}
#endif

#endif
