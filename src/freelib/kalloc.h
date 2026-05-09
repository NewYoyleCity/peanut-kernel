#ifndef KALLOC_H
#define KALLOC_H

#include "kstdint.h"


void* kalloc(size_t size);
void kalloc_init();

void kfree(void* ptr);
void kfree_large(void* ptr);

#endif
