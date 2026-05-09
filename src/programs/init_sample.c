void _start() {
    const char* msg = "Peanut init started!\n";

    __asm__ volatile (
        "mov $1, %%rax\n\t"
        "mov $1, %%rdi\n\t"
        "mov %0, %%rsi\n\t"
        "mov $22, %%rdx\n\t"
        "syscall"
        : : "r"(msg) : "rax", "rdi", "rsi", "rdx"
    );

    while (1) {
        __asm__ volatile (
            "mov $24, %%rax\n\t"
            "syscall"
            : : : "rax"
        );
    }
}
