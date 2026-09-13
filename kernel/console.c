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

void readline(char *buf, int max_len) {
  int i = 0;
  while (i < max_len - 1) {
    char c = kb_getchar();
    if (c == '\n') {
      print("\n", 0);
      break;
    }
    if (c == '\b') {
      if (i > 0) {
        buf[--i] = '\0';
        vga_backspace();
      }
    } else {
      char echo[2] = {c, 0};
      print(echo, 0);
      buf[i++] = c;
    }
  }
  buf[i] = '\0';
}
