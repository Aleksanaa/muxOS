#include "tss.h"
#include "gdt.h"

static tss_t tss;
static uint8_t tss_kernel_stack[4096];

void tss_init() {
  uint32_t base = (uint32_t)&tss;
  uint32_t limit = sizeof(tss_t) - 1;

  uint8_t *p = (uint8_t *)&tss;
  for (uint32_t i = 0; i < sizeof(tss_t); i++)
    p[i] = 0;

  tss.ss0 = 0x10;
  tss.esp0 = (uint32_t)((uintptr_t)(tss_kernel_stack + 4096));
  tss.cs = 0x08;
  tss.ss = tss.ds = tss.es = tss.fs = tss.gs = 0x10;
  tss.iomap_base = sizeof(tss_t);

  gdt_set(5, base, limit, 0x89, 0x00);

  uint16_t sel = 0x28;
  asm volatile("ltr %0" : : "r"(sel));
}

void tss_set_kernel_stack(uint32_t esp0) {
  tss.esp0 = esp0;
}
