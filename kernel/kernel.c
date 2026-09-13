// kernel.c
#include "kernel.h"
#include "fs.h"
#include "gdt.h"
#include "idt.h"
#include "io.h"
#include "keyboard.h"
#include "pic.h"
#include "pmm.h"
#include "process.h"
#include "serial.h"
#include "terminal.h"
#include "tss.h"
#include "vga.h"
#include "vmm.h"

void panic(const char *msg);

static void pit_init(uint32_t hz) {
  uint32_t divisor = 1193180 / hz;
  outb(0x43, 0x36);
  outb(0x40, divisor & 0xFF);
  outb(0x40, (divisor >> 8) & 0xFF);
}
void task_kernel_init();

int kernel_main(uint32_t magic, multiboot_info_t *mbi) {
  asm volatile("cli" ::: "memory");
  if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
    PANIC("Invalid multiboot magic. Not booted by a multiboot loader.");
  }
  // 初始化
  gdt_init();
  tss_init();
  pic_init();
  pit_init(1000);
  idt_init();
  serial_init();
  terminal_init();
  pmm_init(mbi);
  vmm_init();
  keyboard_init();
  fs_init();
  fs_selftest();
  process_register_current();
  process_create_kernel(task_kernel_init);

  for (;;) {
    asm volatile("sti; hlt" ::: "memory");
  }
}

void panic(const char *msg) {
  clear_screen();
  print("\n=== KERNEL PANIC! ===\n", 0);
  print(msg, 0x0c);
  print("\n", 0x0c);
  for (;;)
    ;
}

void task_kernel_init() {
  print("kernel task init!\n", 0x0B);
  int pid = process_create_user();
  if (pid > 0)
    start_user_process(pid, "sh");
  while (1)
    ;
}
