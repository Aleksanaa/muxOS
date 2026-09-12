bits 32

extern user_main

global user_c_start

user_c_start:
    ; 这里不再使用相对地址重算 user_main，而是直接跳到符号地址。
    ; 固定虚拟地址映像下，这种方式更稳定，也避免了 call/pop 计算偏移造成的错位。
    mov eax, user_main
    jmp eax
