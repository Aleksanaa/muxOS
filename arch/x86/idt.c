#include "idt.h"
#include "keyboard.h"
#include "vga.h"
#include <stdint.h>
/* 函数指针 → uint32_t，经 uintptr_t 中转消除编译器警告（32位下等宽） */
#define H(fn) ((uint32_t)(uintptr_t)(fn))

/* 异常处理入口，定义在 isr.c / switch.s */
extern void isr0();         /* #DE 除零异常 */
extern void isr6_stub();    /* #UD 无效指令 */
extern void isr13();        /* #GP 通用保护错误 */
extern void isr14_stub();   /* #PF 页错误 */
extern void irq0_stub();    /* IRQ0 PIT 定时器中断 */
extern void syscall_stub(); /* int 0x80 系统调用入口 */

static idt_entry_t idt[IDT_SIZE];
static idt_ptr_t idtp;

/*
 * idt_set — 填写一个 IDT 描述符
 *
 * type_attr 字节：
 *   0x8E = 1000 1110 → present, DPL=0, 32位中断门（自动关中断）
 *   0xEE = 1110 1110 → present, DPL=3, 32位中断门（允许用户态 int 0x80）
 */
static void idt_set(int i, uint32_t handler, uint16_t selector, uint8_t flags) {
  idt[i].offset_low = handler & 0xFFFF;
  idt[i].offset_high = (handler >> 16) & 0xFFFF;
  idt[i].selector = selector;
  idt[i].zero = 0;
  idt[i].type_attr = flags;
}

void idt_init() {
  idtp.limit = sizeof(idt) - 1;
  idtp.base = (uint32_t)(unsigned long)&idt;

  /* 先把所有条目清零，未注册的向量触发时不会跳到随机地址 */
  for (int i = 0; i < IDT_SIZE; i++)
    idt_set(i, 0, 0, 0);

  /* CPU 异常（向量 0–31，DPL=0，内核态处理） */
  idt_set(0, H(isr0), 0x08, 0x8E);        /* #DE 除零 */
  idt_set(6, H(isr6_stub), 0x08, 0x8E);   /* #UD 无效指令 */
  idt_set(13, H(isr13), 0x08, 0x8E);      /* #GP 通用保护错误 */
  idt_set(14, H(isr14_stub), 0x08, 0x8E); /* #PF 页错误 */

  /* 硬件中断（向量 32–47，PIC 重映射后的 IRQ） */
  idt_set(32, H(irq0_stub), 0x08, 0x8E);          /* IRQ0 PIT 定时器 */
  idt_set(0x21, H(keyboard_handler), 0x08, 0x8E); /* IRQ1 键盘 */

  /* 系统调用（向量 0x80，DPL=3 允许用户态 int 0x80） */
  idt_set(0x80, H(syscall_stub), 0x08, 0xEE);

  asm volatile("lidt %0" : : "m"(idtp)); /* 加载 IDT 寄存器 */

  print("[OK] IDT init\n", 0);
}
