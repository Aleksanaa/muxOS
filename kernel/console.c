#include "console.h"
#include "keyboard.h"
#include "vga.h"

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
