section .text
[bits 64]
global irq_timer
global isr_invalid_opcode
global isr_general_protection
global isr_page_fault
extern irq_timer_dispatch
extern cpu_exception_handler

irq_timer:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, rsp
    call irq_timer_dispatch
    mov rsp, rax
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    iretq

isr_invalid_opcode:
    cld
    mov rdi, 6
    xor rsi, rsi
    mov rdx, [rsp]
    call cpu_exception_handler
.halt_ud:
    cli
    hlt
    jmp .halt_ud

isr_general_protection:
    cld
    mov rdi, 13
    mov rsi, [rsp]
    mov rdx, [rsp + 8]
    call cpu_exception_handler
.halt_gp:
    cli
    hlt
    jmp .halt_gp

isr_page_fault:
    cld
    mov rdi, 14
    mov rsi, [rsp]
    mov rdx, [rsp + 8]
    call cpu_exception_handler
.halt_pf:
    cli
    hlt
    jmp .halt_pf
