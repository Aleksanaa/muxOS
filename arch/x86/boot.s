; boot.s

section .multiboot
align 4
    dd 0x1BADB002          ; magic number
    dd 0x00                ; flags
    dd -(0x1BADB002 + 0x00) ; checksum

section .text
global _start
extern kernel_main

_start:
    mov esp, stack_top

    ; Go to kernel
    push ebx        ; multiboot info*
    push eax        ; 
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

; stack
section .bss
align 16
stack_bottom:
    resb 16384   ; 16KB 栈
stack_top: