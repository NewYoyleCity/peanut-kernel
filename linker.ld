ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)

SECTIONS
{
    /* Load the kernel at 1MB (KASLR will randomize at runtime) */
    . = 1M;

    .multiboot_header : ALIGN(8)
    {
        KEEP(*(.multiboot_header))
    }

    .text : ALIGN(16)
    {
        *(.text)
    }

    .rodata : { *(.rodata) }
    .data   : { *(.data) }
    .init_array : {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    }
    .fini_array : {
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
    }
    .bss    : { *(.bss) }

    .initramfs : ALIGN(4096) {
        _initramfs_start = .;
        *(.initramfs)
        _initramfs_end = .;
    }
    
    _kernel_end = .;
}
