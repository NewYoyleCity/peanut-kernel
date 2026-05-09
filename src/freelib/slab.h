#ifndef SLAB_H
#define SLAB_H

#include "freelib/kstdint.h"

void slab_init(void);
void* slab_alloc(size_t size);
int slab_free(void* ptr);

#endif
