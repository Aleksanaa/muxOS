#include "io.h"

/*
 * 8259A PIC（可编程中断控制器）初始化
 *
 * x86 有两片级联的 8259A：主片（PIC1）和从片（PIC2）。
 * BIOS 默认把 IRQ0–7 映射到向量 0x08–0x0F，与 CPU 异常冲突，
 * 必须重映射：IRQ0–7 → 0x20–0x27，IRQ8–15 → 0x28–0x2F。
 *
 * 初始化流程固定为四条命令字（ICW1–ICW4），顺序不能乱。
 * 端口：主片命令 0x20 / 数据 0x21，从片命令 0xA0 / 数据 0xA1。
 */
void pic_init(void) {
  /* ICW1：启动初始化，级联模式，需要 ICW4 */
  outb(0x20, 0x11); io_wait();
  outb(0xA0, 0x11); io_wait();

  /* ICW2：设置中断向量偏移（重映射，避开 CPU 异常 0–31） */
  outb(0x21, 0x20); io_wait(); /* 主片 IRQ0–7  → 向量 0x20–0x27 */
  outb(0xA1, 0x28); io_wait(); /* 从片 IRQ8–15 → 向量 0x28–0x2F */

  /* ICW3：告知主片从片挂在 IRQ2，告知从片自己的级联编号 */
  outb(0x21, 0x04); io_wait(); /* 主片：IRQ2 引脚接从片（bit2=1） */
  outb(0xA1, 0x02); io_wait(); /* 从片：级联编号 2 */

  /* ICW4：8086 模式（非 MCS-80），手动 EOI */
  outb(0x21, 0x01); io_wait();
  outb(0xA1, 0x01); io_wait();

  /* 屏蔽所有中断，再单独开放需要的 IRQ */
  outb(0x21, 0xFF); /* 主片全屏蔽 */
  outb(0xA1, 0xFF); /* 从片全屏蔽 */

  /* 开放 IRQ0（PIT 定时器）和 IRQ1（键盘） */
  outb(0x21, 0xFC); /* 主片：bit0=0 开放 IRQ0，bit1=0 开放 IRQ1 */
  outb(0xA1, 0xFF); /* 从片：全屏蔽 */
}

/*
 * pic_eoi — 发送中断结束信号（End Of Interrupt）
 *
 * 每次硬件中断处理完毕后必须调用，否则 PIC 不会再发同级中断。
 * IRQ8–15 来自从片，需要同时通知主片和从片。
 */
void pic_eoi(uint8_t irq) {
  if (irq >= 8)
    outb(0xA0, 0x20); /* 从片 EOI */
  outb(0x20, 0x20);   /* 主片 EOI */
}
