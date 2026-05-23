/* kpanic.c -- Kernel panic handler.
 *
 * Prints a panic message, disables interrupts, and halts forever.
 */

#include "kpanic.h"
#include "kstdio.h"

void kpanic(const char* message) {
    kprint("\nKERNEL PANIC: ");
    kprint(message);
    kprint("\n");

    __asm__ volatile("cli");
    while (1) {
        __asm__ volatile("hlt");
    }
}
