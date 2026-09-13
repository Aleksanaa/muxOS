global enter_usermode

enter_usermode:
    ; 不要在 0x08000000 这样的低地址区域放用户镜像，
    ; 该区域已被内核的恒等映射占用，会和内核地址空间冲突。
    ; 参数仍在内核栈上。必须先用内核数据段取出它们；若先把 DS
    ; 改为用户段，内核栈（通常在用户映射范围外）的读取会触发 #PF。
    mov eax, [esp + 4]  ; entry
    mov ecx, [esp + 8]  ; user stack

    mov dx, 0x23
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov dx, 0x33        ; user TLS selector (%gs base = TCB)
    mov gs, dx

    push 0x23           ; SS
    push ecx            ; ESP
    pushfd
    or dword [esp], 0x200
    push 0x1B           ; CS
    push eax            ; EIP
    iret
