#ifndef KSTDIO_H
#define KSTDIO_H

#include "kstdint.h"

void kclear();
void kputc(char c);
void kprint(const char* str);
void kprint_hex(uint64_t n);
void kprint_int(int64_t n);

#endif
