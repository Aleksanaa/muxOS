#include "console.h"
#include "keyboard.h"
#include "serial.h"
#include "vga.h"

/* Wait for a character from either the PS/2 keyboard or COM1. */
char console_getchar_src(int *from_kb) {
  while (!kb_haschar() && !serial_haschar()) {
  }
  if (kb_haschar()) {
    if (from_kb)
      *from_kb = 1;
    return kb_getchar();
  }
  if (from_kb)
    *from_kb = 0;
  return serial_getchar();
}

char console_getchar(void) { return console_getchar_src(0); }
