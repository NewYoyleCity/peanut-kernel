; decompress_entry.asm - Kernel decompressor entry point
; Sets up long mode, decompresses the kernel payload appended at
; a fixed address (0x400000 + 0x1000), and jumps to the result at 16MB.

extern decompress_kernel

BITS 32

section .multiboot_header
align 8
header_start:
    dd 0xE85250D6
    dd 0
    dd header_end - header_start
    dd -(0xE85250D6 + 0 + (header_end - header_start))
    ; framebuffer tag
    dw 5
    dw 1
    dd 20
    dd 1024
    dd 768
    dd 32
    dw 0, 0
    ; end tag
    dw 0, 0
    dd 8
header_end:

section .text
global _start
_start:
    cli
    mov esp, stack_top

    ; Check CPUID
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 0x200000
    push eax
    popfd
    pushfd
    pop eax
    xor eax, ecx
    push ecx
    popfd
    test eax, 0x200000
    jz .done

    ; Check extended functions
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .done

    ; Check long mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .done

    ; Set up page tables at 0x9000
    mov edi, 0x9000
    mov cr3, edi
    xor eax, eax
    mov ecx, 4096
    rep stosb

    ; PML4[0] -> PDPT at 0x8000
    mov dword [0x9000], 0x8003
    ; PML4[0] -> PDPT at 0x8000 (high dword = 0)
    ; Already zero from rep stosb

    ; PDPT[0] -> PD at 0x7000 (covers 0-1GB)
    mov dword [0x8000], 0x7003
    ; PDPT[1] -> PD at 0x6000 (covers 1-2GB)
    mov dword [0x8008], 0x6003

    ; Map PD[0] at 0x7000: identity map 0-128MB
    mov edi, 0x7000
    mov eax, 0x83 | (1 << 7)
    mov ecx, 64
.pt_loop:
    mov [edi], eax
    add edi, 8
    add eax, 0x200000
    loop .pt_loop

    ; Map PD[1] at 0x6000: 128-256MB
    mov edi, 0x6000
    mov eax, 0x83 | (1 << 7) | 0x8000000
    mov ecx, 64
.pt_loop2:
    mov [edi], eax
    add edi, 8
    add eax, 0x200000
    loop .pt_loop2

    ; Enable PAE + SMEP + SMAP
    mov eax, cr4
    or eax, 1 << 5       ; PAE
    or eax, 1 << 20      ; SMEP
    or eax, 1 << 21      ; SMAP
    mov cr4, eax

    ; Enable long mode + NXE
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8       ; LME
    or eax, 1 << 11      ; NXE
    wrmsr

    ; Enable paging + WP
    mov eax, cr0
    or eax, 0x80000001   ; PG + PE
    or eax, 1 << 16      ; WP
    mov cr0, eax

    ; Load GDT and jump to 64-bit
    lgdt [gdt_ptr]
    jmp 0x08:.long_mode

.done:
    hlt
    jmp .done

BITS 64
.long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Call decompress_kernel(dst=16MB, dst_len=4MB, src=0x4001000, src_len=?)
    ; The compressed data is at virtual address 0x4001000 (4MB + 4KB)
    mov rdi, 0x1000000
    mov rsi, 0x4001000
    mov rdx, 0x400000
    mov rcx, [compressed_data_len]
    call decompress_kernel

    ; Jump to decompressed kernel at its original VMA offset
    ; kernel _start is at 0x100000 + 0x30 = 0x100030
    ; We decompressed to 0x1000000, so real entry = 0x1000000 + 0x30 = 0x1000030
    mov rax, 0x1000030
    jmp rax

section .data
align 8
gdt:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_ptr:
    dw $ - gdt - 1
    dq gdt

compressed_data_len:
    dq 0

section .bss
align 16
stack:
    resb 16384
stack_top:
