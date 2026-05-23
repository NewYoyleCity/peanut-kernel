ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)

SECTIONS
{
    . = 1M;
    .multiboot_header : ALIGN(8) { KEEP(*(.multiboot_header)) }
    .text : ALIGN(16) { *(.text) }
    .data : ALIGN(16) { *(.data) }
    .bss  : ALIGN(16) { *(.bss) }
    _end = .;
}
