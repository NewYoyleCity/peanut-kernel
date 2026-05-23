#include "freelib/kstdint.h"
#include "freelib/kpanic.h"
#include "config.h"

#ifdef CONFIG_STACK_CANARY
uintptr_t __stack_chk_guard = 0xDEADBEEFCAFEBABEull;

__attribute__((used))
void __stack_chk_fail(void) {
    kpanic("[Peanut kernel - panic - Stack smashing detected!]");
    for (;;) __asm__ volatile("cli; hlt");
}
#endif
