#include "gdt.h"
#include "vga.h"

static struct gdt_entry gdt[6];
static struct gdt_ptr gp;

/*
 * gdt_set — 填写一个 GDT 描述符
 *
 * x86 的 GDT 条目把 base 和 limit 拆散存放在不连续的字段里，
 * 这是为了兼容 80286 留下的历史包袱。
 *
 * access 字节（8位）：
 *   7: present  6-5: DPL  4: S(1=代码/数据段)  3-0: type
 *   0x9A = 1001 1010 → present, DPL=0, 代码段, 可执行+可读
 *   0x92 = 1001 0010 → present, DPL=0, 数据段, 可写
 *   0xFA = 1111 1010 → present, DPL=3, 代码段, 可执行+可读（用户态）
 *   0xF2 = 1111 0010 → present, DPL=3, 数据段, 可写（用户态）
 *
 * gran 字节高4位：
 *   0xCF = 1100 xxxx → 4KB 页粒度，32位保护模式
 */
void gdt_set(int i, unsigned int base, unsigned int limit, unsigned int access,
             unsigned int gran) {
  gdt[i].base_low = base & 0xFFFF;
  gdt[i].base_middle = (base >> 16) & 0xFF;
  gdt[i].base_high = (base >> 24) & 0xFF;

  gdt[i].limit_low = limit & 0xFFFF;
  gdt[i].granularity = (limit >> 16) & 0x0F;
  gdt[i].granularity |= gran & 0xF0;

  gdt[i].access = access;
}

void gdt_init() {
  gp.limit = sizeof(gdt) - 1;
  gp.base = (unsigned int)(unsigned long)&gdt;

  gdt_set(0, 0, 0, 0, 0);                // 空描述符（x86 规定第0项必须为空）
  gdt_set(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // 内核代码段  选择子 0x08
  gdt_set(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // 内核数据段  选择子 0x10
  gdt_set(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); // 用户代码段  选择子 0x1B（RPL=3）
  gdt_set(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // 用户数据段  选择子 0x23（RPL=3）
  gdt_set(5, 0, 0, 0, 0);                // TSS 占位，由 tss_init() 填写

  asm volatile("lgdt %0" : : "m"(gp));

  // 重新加载各段寄存器，使其指向新的 GDT 描述符
  asm volatile("mov $0x10, %%ax\n" // 切换到数据段
               "mov %%ax, %%ds\n"
               "mov %%ax, %%es\n"
               "mov %%ax, %%fs\n"
               "mov %%ax, %%gs\n"
               "mov %%ax, %%ss\n"
               "ljmp $0x08, $1f\n" // 切换代码段
               "1:\n"
               :
               :
               : "eax");

  print("[OK] GDT init\n", 0);
}
