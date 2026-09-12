#include "io.h"
#include "kernel.h"
#include "vga.h"

extern void print(const char *, unsigned char);

void isr0() {
  PANIC("INFO: Divide by zero");
  for (;;)
    ;
}

void isr6_handler(uint32_t fault_eip, uint32_t fault_cs,
                  uint32_t fault_eflags) {
  print("Invalid Opcode(6)!\n", 0x04);
  print("EIP=0x", 0x04);
  print_hex(fault_eip);
  print(" CS=0x", 0x04);
  print_hex(fault_cs);
  print(" EFLAGS=0x", 0x04);
  print_hex(fault_eflags);
  print("\n", 0x04);
  print("Inst=0x", 0x04);
  print_hex(*(uint32_t *)(uintptr_t)fault_eip);
  print("\n", 0x04);
  for (;;)
    asm volatile("hlt");
}

void isr13_handler(uint32_t error_code) {
  print("GPF(13)! Error code: 0x", 0x0C);
  for (int i = 28; i >= 0; i -= 4) {
    char hex = (error_code >> i) & 0xF;
    hex = hex < 10 ? '0' + hex : 'A' + hex - 10;
    char h[2] = {hex, 0};
    print(h, 0x0C);
  }
  print("\n", 0x0C);
  for (;;)
    asm volatile("hlt");
}

void isr14_handler(uint32_t error_code, uint32_t fault_eip, uint32_t fault_cs,
                   uint32_t fault_esp) {
  print("Page Fault(14)! Error code: 0x", 0x04);
  for (int i = 28; i >= 0; i -= 4) {
    char hex = (error_code >> i) & 0xF;
    hex = hex < 10 ? '0' + hex : 'A' + hex - 10;
    char h[2] = {hex, 0};
    print(h, 0x04);
  }
  print("\n", 0x04);
  uint32_t cr2;
  asm volatile("mov %%cr2, %0" : "=r"(cr2));
  print("Faulting address: 0x", 0x04);
  for (int i = 28; i >= 0; i -= 4) {
    char hex = (cr2 >> i) & 0xF;
    hex = hex < 10 ? '0' + hex : 'A' + hex - 10;
    char h[2] = {hex, 0};
    print(h, 0x04);
  }
  print("\n", 0x04);

  print("Fault EIP=0x", 0x04);
  print_hex(fault_eip);
  print(" CS=0x", 0x04);
  print_hex(fault_cs);
  print(" ESP=0x", 0x04);
  print_hex(fault_esp);
  print("\n", 0x04);
  print("Code=0x", 0x04);
  print_hex(*(uint32_t *)(uintptr_t)fault_eip);
  print(" Stack=0x", 0x04);
  print_hex(*(uint32_t *)(uintptr_t)fault_esp);
  print("\n", 0x04);

  for (;;)
    asm volatile("hlt");
}

void isr13() {
  uint32_t error_code;
  asm volatile("mov 4(%%ebp), %0" : "=r"(error_code));
  isr13_handler(error_code);
}
