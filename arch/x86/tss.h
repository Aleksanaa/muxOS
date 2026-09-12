#ifndef TSS_H
#define TSS_H
#include <stdint.h>

typedef struct {
  uint32_t prev_tss;
  uint32_t esp0, ss0;
  uint32_t esp1, ss1;
  uint32_t esp2, ss2;
  uint32_t cr3, eip, eflags;
  uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
  uint32_t es, cs, ss, ds, fs, gs;
  uint32_t ldt;
  uint16_t trap, iomap_base;
} __attribute__((packed)) tss_t;

void tss_init();
void tss_set_kernel_stack(uint32_t esp0);

#endif
