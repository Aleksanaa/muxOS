global irq0_stub
global context_switch
global process_enter
global process_jump
global syscall_stub
global isr6_stub
global isr14_stub
extern process_tick
extern syscall_handler
extern isr6_handler
extern isr14_handler
extern processes
extern current
extern process_count
extern syscall_kernel_esp
extern tss_set_kernel_stack
extern process_set_tls
extern signal_deliver

; process_t 各字段在结构体中的偏移（与 process.h 保持同步）
PROCESS_SIZE      equ 460 ; sizeof(process_t) (guarded by _Static_assert in process.h)
CTX_ESP_OFF       equ 4   ; offsetof(process_t, ctx.esp)
STATE_OFF         equ 24  ; offsetof(process_t, state)
STARTED_OFF       equ 28  ; offsetof(process_t, started)
KERNEL_STACK_OFF  equ 32  ; offsetof(process_t, kernel_stack)
SLEEP_TICKS_OFF   equ 36  ; offsetof(process_t, sleep_ticks)
PDIR_OFF          equ 248 ; offsetof(process_t, pdir)
PROC_ZOMBIE       equ 2

; -----------------------------------------------------------------------
; isr14_stub — 页故障入口
;
; CPU 在进入异常处理器时压入：error_code, EIP, CS, EFLAGS,
; 以及从 ring 3 进入时的 ESP_user, SS_user。保存原始 ESP 后再调用 C，
; 避免 C 函数序言改变异常帧的定位。
; -----------------------------------------------------------------------
isr6_stub:
    mov eax, esp
    push dword [eax + 8]  ; EFLAGS
    push dword [eax + 4]  ; CS
    push dword [eax]      ; EIP
    call isr6_handler
    add esp, 12
    hlt
    jmp $

isr14_stub:
    mov eax, esp
    push dword [eax + 16] ; ESP_user
    push dword [eax + 8]  ; CS
    push dword [eax + 4]  ; EIP
    push dword [eax]      ; error code
    call isr14_handler
    add esp, 16
.page_fault_halt:
    cli
    hlt
    jmp .page_fault_halt

; -----------------------------------------------------------------------
; irq0_stub — PIT 定时器中断处理（IRQ0，向量 0x20）
;
; 从用户态（ring 3）进入时，CPU 自动切换到 TSS 指定的内核栈，
; 并依次压入：SS_user, ESP_user, EFLAGS, CS, EIP（共 5 个 dword）。
; 从内核态（ring 0）进入时只压入：EFLAGS, CS, EIP（共 3 个 dword）。
;
; pusha 之后内核栈布局（以 ring 3 为例）：
;   esp+0  edi  \
;   esp+4  esi   |
;   esp+8  ebp   | pusha 保存的 8 个寄存器（32 字节）
;   esp+12 esp*  | （pusha 保存的 esp 值，无实际意义）
;   esp+16 ebx   |
;   esp+20 edx   |
;   esp+24 ecx   |
;   esp+28 eax  /
;   esp+32 EIP   \
;   esp+36 CS     | CPU 压入的 iret 帧
;   esp+40 EFLAGS |
;   esp+44 ESP_user（ring 3 才有）
;   esp+48 SS_user /
; -----------------------------------------------------------------------
irq0_stub:
    pusha ; push 寄存器

    ; 从 ring 3 进入中断时，DS/ES/FS/GS 仍是用户选择子。中断处理器
    ; 随后要读写内核全局变量，因此先切换到内核数据段；popa 会恢复
    ; 原始通用寄存器，返回用户态前在 .done 中恢复用户段。
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; 发送 EOI（End Of Interrupt）给主片，否则 PIC 不再发同级中断
    mov al, 0x20
    out 0x20, al

    ; 在调用 process_tick 之前保存旧的 current 索引
    ; process_tick 内部会更新 current，之后就拿不到旧值了
    mov esi, [current]

    ; process_tick 递减所有进程的 sleep_ticks，返回下一个要运行的进程索引
    ; 返回 -1 表示不需要切换
    call process_tick
    cmp eax, -1
    je .done 

    ; 把当前内核栈指针（pusha 帧顶）保存到旧进程的 ctx.esp
    ; 恢复时 popa+iret 就能正确还原旧进程的执行现场
    mov ecx, esi
    imul ecx, PROCESS_SIZE
    add ecx, processes
    mov [ecx + CTX_ESP_OFF], esp

    ; eax 是新进程索引（process_tick 已更新 current），计算 &processes[new]
    imul eax, PROCESS_SIZE
    add eax, processes

    ; 如果新进程是第一次运行，标记 started=1（ctx.esp 指向手工构造的初始帧）
    mov ecx, [eax + STARTED_OFF]
    test ecx, ecx
    jnz .already_started
    mov dword [eax + STARTED_OFF], 1

.already_started:
    cmp dword [eax + KERNEL_STACK_OFF], 0
    je .kernel_stack_ready
    ; 更新 TSS esp0 为新进程的内核栈顶
    push eax                          ; 保存 &processes[new]
    push dword [eax + KERNEL_STACK_OFF]
    call tss_set_kernel_stack
    add esp, 4
    pop eax                           ; 恢复 &processes[new]

.kernel_stack_ready:
    ; 恢复目标进程的 %gs 基址（mlibc TCB）
    push eax
    push eax
    call process_set_tls
    add esp, 4
    pop eax
    ; 切换到新进程的地址空间
    mov ecx, [eax + PDIR_OFF]
    mov cr3, ecx
    ; 切换内核栈到新进程保存的 esp
    mov esp, [eax + CTX_ESP_OFF]

    ; 检查 iret 帧里的 CS：若 RPL=3 说明要返回用户态，需要恢复用户段寄存器
    ; CS 在 pusha 帧（32字节）之后，偏移 36（EIP=32, CS=36）
    mov eax, [esp + 36]
    test eax, 3         ; 低2位是 RPL，非零表示用户态
    jz .done

    mov ax, 0x23        ; 用户数据段选择子（DPL=3）
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov ax, 0x33        ; 用户 TLS 选择子（%gs base 由 set_thread_area 设置）
    mov gs, ax

.done:
    ; 若中断来自用户态，iret 不会自动恢复 DS/ES/FS/GS。
    mov eax, [esp + 36]
    test eax, 3
    jz .segments_restored
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov ax, 0x33
    mov gs, ax
.segments_restored:
    popa ; pop 寄存器
    iret ; 恢复


; -----------------------------------------------------------------------
; syscall_stub — int 0x80 系统调用入口
;
; 用户态执行 int 0x80 时，CPU 切换到内核栈并压入：
;   SS_user, ESP_user, EFLAGS, CS, EIP
; 约定：eax=系统调用号，ebx/ecx/edx=参数1/2/3
; -----------------------------------------------------------------------
syscall_stub:
    ; 切换到内核数据段（int 0x80 不自动切换 DS/ES）
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    pop eax

    ; 保存当前内核栈指针供 sys_fork 使用
    ; 此时 esp 指向 iret 帧（EIP,CS,EFLAGS,ESP_user,SS_user）
    mov [syscall_kernel_esp], esp

    ; 保存所有用户态寄存器（供 fork 复制完整现场）
    pusha

    ; 按 C 调用约定从右到左压参数
    ; syscall_handler(eax, ebx, ecx, edx, esi, edi, ebp)
    ; 从 pusha 帧里取出原始参数（pusha 布局见 irq0_stub 注释）
    mov eax, [esp + 28]   ; eax (syscall number)
    mov ebx, [esp + 16]   ; ebx (arg1)
    mov ecx, [esp + 24]   ; ecx (arg2)
    mov edx, [esp + 20]   ; edx (arg3)
    mov esi, [esp + 4]    ; esi (arg4)
    mov edi, [esp + 0]    ; edi (arg5)
    mov ebp, [esp + 8]    ; ebp (arg6)
    push ebp
    push edi
    push esi
    push edx
    push ecx
    push ebx
    push eax
    call syscall_handler
    ; Blocking handlers can enable IRQs; keep return and scheduling atomic.
    cli
    ; patch eax return value back into pusha frame (7 args = 28 bytes above)
    mov [esp + 56], eax
    add esp, 28           ; 清除压入的 7 个参数

    ; 返回用户态前投递挂起的信号（可能改写 iret 帧进入 handler）
    call signal_deliver
    test eax, eax
    jnz .no_sleep

    ; 检查当前进程是否是 ZOMBIE（SYS_EXIT 设置）或 sleep_ticks > 0
    mov ecx, [current]
    imul ecx, PROCESS_SIZE
    add ecx, processes
    mov edx, [ecx + STATE_OFF]
    cmp edx, PROC_ZOMBIE
    je .need_switch
    mov edx, [ecx + SLEEP_TICKS_OFF]
    test edx, edx
    jnz .need_switch
    jmp .no_sleep

.need_switch:

    ; pusha 帧已经在上面构造好了，直接保存当前 esp 并切换进程
    mov esi, [current]

    call process_tick
    cmp eax, -1
    jne .do_switch

    ; process_tick 返回 -1：没有其他可运行进程
    ; 如果当前进程是 ZOMBIE，强制唤醒父进程
    mov ecx, [current]
    imul ecx, PROCESS_SIZE
    add ecx, processes
    mov edx, [ecx + STATE_OFF]
    cmp edx, PROC_ZOMBIE
    jne .sleep_done     ; 不是 ZOMBIE，正常 sleep

    ; 是 ZOMBIE：扫描所有进程，清除 sleep_ticks，让 process_tick 能找到父进程
    push esi
    xor esi, esi
.wake_loop:
    mov ecx, [process_count]
    cmp esi, ecx
    jge .wake_done
    mov ecx, esi
    imul ecx, PROCESS_SIZE
    add ecx, processes
    mov dword [ecx + SLEEP_TICKS_OFF], 0
    inc esi
    jmp .wake_loop
.wake_done:
    pop esi
    call process_tick
    cmp eax, -1
    je .sleep_done      ; 还是没有，只能等
    jmp .zombie_found

.zombie_found:
    imul eax, PROCESS_SIZE
    add eax, processes
    cmp dword [eax + KERNEL_STACK_OFF], 0
    je .zombie_tss_ready
    push eax                          ; 保存 &processes[new]
    push dword [eax + KERNEL_STACK_OFF]
    call tss_set_kernel_stack
    add esp, 4
    pop eax                           ; 恢复 &processes[new]
.zombie_tss_ready:
    push eax
    push eax
    call process_set_tls
    add esp, 4
    pop eax
    mov ecx, [eax + STARTED_OFF]
    test ecx, ecx
    jnz .zombie_switch_started
    mov dword [eax + STARTED_OFF], 1
.zombie_switch_started:
    mov ecx, [eax + PDIR_OFF]
    mov cr3, ecx
    mov esp, [eax + CTX_ESP_OFF]
    jmp .sleep_done

.do_switch:

    ; 保存旧进程的内核栈指针（pusha 帧顶）
    mov ecx, esi
    imul ecx, PROCESS_SIZE
    add ecx, processes
    mov [ecx + CTX_ESP_OFF], esp

    ; 切换到新进程
    imul eax, PROCESS_SIZE
    add eax, processes

    ; 更新 TSS esp0
    cmp dword [eax + KERNEL_STACK_OFF], 0
    je .sleep_tss_ready
    push eax                          ; 保存 &processes[new]
    push dword [eax + KERNEL_STACK_OFF]
    call tss_set_kernel_stack
    add esp, 4
    pop eax                           ; 恢复 &processes[new]

.sleep_tss_ready:
    push eax
    push eax
    call process_set_tls
    add esp, 4
    pop eax
    mov ecx, [eax + STARTED_OFF]
    test ecx, ecx
    jnz .sleep_already_started
    mov dword [eax + STARTED_OFF], 1

.sleep_already_started:
    mov ecx, [eax + PDIR_OFF]
    mov cr3, ecx
    mov esp, [eax + CTX_ESP_OFF]

.sleep_done:
.no_sleep:
    ; IRET does not restore data segments; use the target frame's privilege.
    mov ax, 0x10
    test dword [esp + 36], 3
    jz .return_segments_ready
    mov ax, 0x23
.return_segments_ready:
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov bx, 0x10
    test dword [esp + 36], 3
    jz .return_gs_ready
    mov bx, 0x33
.return_gs_ready:
    mov gs, bx
    popa
    iret


; -----------------------------------------------------------------------
; context_switch(context_t *old, context_t *new)
;
; 保存当前 C 调用现场到 *old，从 *new 恢复并 ret 回调用点。
; 用于内核进程之间的主动切换（process_exit 等）。
; 注意：ctx.esp 保存的是 C 栈帧指针，不是 pusha 帧，不能用 popa+iret 恢复。
; -----------------------------------------------------------------------
context_switch:
    mov eax, [esp+4]    ; old ctx*
    mov ecx, [esp+8]    ; new ctx*

    ; 保存旧进程的 callee-saved 寄存器
    mov [eax+0],  esp
    mov [eax+4],  ebp
    mov [eax+8],  ebx
    mov [eax+12], esi
    mov [eax+16], edi

    ; 恢复新进程的 callee-saved 寄存器
    mov esp, [ecx+0]
    mov ebp, [ecx+4]
    mov ebx, [ecx+8]
    mov esi, [ecx+12]
    mov edi, [ecx+16]
    ret                 ; ret 到新进程上次调用 context_switch 的返回地址


; -----------------------------------------------------------------------
; process_enter(context_t *old, context_t *new)
;
; 保存旧进程的 C 现场，然后从 new->ctx.esp（pusha+iret 帧）恢复新进程。
; 用于进程第一次被调度时（ctx.esp 指向手工构造的初始帧）。
; -----------------------------------------------------------------------
process_enter:
    mov eax, [esp+4]    ; old ctx*
    mov ecx, [esp+8]    ; new ctx*

    ; 保存旧进程现场
    mov [eax+0],  esp
    mov [eax+4],  ebp
    mov [eax+8],  ebx
    mov [eax+12], esi
    mov [eax+16], edi

    ; 切换到新进程的内核栈（pusha+iret 帧）
    mov esp, [ecx+0]

    ; 若目标是用户态（CS RPL=3），先设置用户段寄存器
    mov eax, [esp+36]
    test eax, 3
    jz .ke

    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov ax, 0x33
    mov gs, ax
.ke:
    popa
    iret


; -----------------------------------------------------------------------
; process_jump(context_t *new)
;
; 无条件跳入目标进程，不保存当前现场。
; 用于 process_exit：当前进程已销毁，直接跳到下一个进程。
; -----------------------------------------------------------------------
process_jump:
    mov ecx, [esp+4]    ; new ctx*
    mov esp, [ecx+0]    ; 恢复目标进程的内核栈（pusha+iret 帧）
    mov eax, [esp+36]    ; iret 帧中的 CS
    test eax, 3
    jz .jump_kernel
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov ax, 0x33
    mov gs, ax
.jump_kernel:
    popa
    iret
