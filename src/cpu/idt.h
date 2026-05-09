#ifndef IDT_H
#define IDT_H

#include "freelib/kstdint.h"

void idt_init(void);
void idt_set_handler(uint8_t vector, void (*handler)(void));
void cpu_exception_handler(uint64_t vector, uint64_t error, uint64_t rip);

#endif
