#ifndef INFLATE_H
#define INFLATE_H

#include "freelib/kstdint.h"

uint32_t inflate(void* dst, uint32_t dst_len, const void* src, uint32_t src_len);

#endif
