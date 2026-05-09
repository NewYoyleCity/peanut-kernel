
section .multiboot_header
align 8
header_start:
    dd 0xe85250d6
    dd 0
    dd header_end - header_start
    dd 0x100000000 - (0xe85250d6 + 0 + (header_end - header_start))

    dw 5
    dw 0
    dd 20
    dd 1024
    dd 768
    dd 32
    align 8

    dw 0
    dw 0
    dd 8
header_end:

section .text
[bits 32]
global _start
_start:
    cli
    mov esp, stack_top
    mov [multiboot_info_ptr], ebx

    call setup_page_tables
    call enable_paging

    lgdt [gdt64.pointer]
    jmp gdt64.code_segment:long_mode_start

; Simple KASLR randomization using RDTSC
kaslr_randomize:
    ; Read timestamp counter as entropy source
    rdtsc
    ; Use lower 32 bits and mask to get a random offset
    ; We'll randomize in 2MB increments
    and eax, 0x3FF  ; Keep lower 10 bits (0-1023)
    shl eax, 21     ; Multiply by 2MB (2^21)
    ; Add base offset (start at 0x10000000 for safety)
    or eax, 0x10000000
    ret

setup_page_tables:
    mov eax, pdpt
    or eax, 0b111
    mov [pml4], eax

    mov eax, pd0
    or eax, 0b111
    mov [pdpt], eax

    mov eax, pd1
    or eax, 0b111
    mov [pdpt + 8], eax

    mov eax, pd2
    or eax, 0b111
    mov [pdpt + 16], eax

    mov eax, pd3
    or eax, 0b111
    mov [pdpt + 24], eax



    mov ebx, pd0
    mov ecx, 0
    mov eax, 0b10000111
.map_pd:
    mov edx, ecx
    and edx, 511
    mov [ebx + edx * 8], eax
    mov dword [ebx + edx * 8 + 4], 0
    add eax, 0x200000
    inc ecx
    test ecx, 511
    jne .same_pd
    add ebx, 4096
.same_pd:
    cmp ecx, 2048
    jne .map_pd
    ret

enable_paging:
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov eax, pml4
    mov cr3, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    ret

[bits 64]
global interrupt_default_entry
global syscall_entry

long_mode_start:
    mov ax, gdt64.data_segment
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    extern kmain
    mov edi, [multiboot_info_ptr]
    xor esi, esi  ; The boot path passes a zero relocation offset.
    call kmain
    hlt

interrupt_default_entry:
    cli
    cld
    extern interrupt_default_handler
    call interrupt_default_handler
.halt:
    hlt
    jmp .halt

syscall_entry:
    mov r10, rcx
    mov r8, rsp
    push rcx
    push r11

    mov r9, r8
    mov r8, r10
    push r11

    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax

    extern syscall_handler
    call syscall_handler

    add rsp, 8
    pop r11
    pop rcx
    o64 sysret

section .rodata
gdt64:
    dq 0
.code_segment: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)
.data_segment: equ $ - gdt64
    dq (1<<41) | (1<<44) | (1<<47)
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

section .bss
align 4096
pml4: resb 4096
pdpt: resb 4096
pd0:  resb 4096
pd1:  resb 4096
pd2:  resb 4096
pd3:  resb 4096
stack_bottom:
    resb 4096 * 4
stack_top:
multiboot_info_ptr:
    resd 1
