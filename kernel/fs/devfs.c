/*
 * devfs.c - trivial character devices behind the VFS.
 *
 * fd 0/1/2 point at /dev/console; writes go to the VGA console (which also
 * mirrors to the serial port), reads pull from the keyboard buffer.
 */

#include "console.h"
#include "fs.h"
#include "vga.h"

struct devsw devsw[NDEV];

static int console_write(const void *buf, int n) {
  const char *b = (const char *)buf;
  for (int i = 0; i < n; i++) {
    char s[2] = {b[i], 0};
    print(s, 0x07);
  }
  return n;
}

static int console_read(void *buf, int n) {
  asm volatile("sti");
  char *b = (char *)buf;
  int i = 0;
  while (i < n) {
    char c = console_getchar();
    b[i++] = c;
    if (c == '\n' || c == '\r')
      break;
  }
  return i;
}

static int null_write(const void *buf, int n) {
  (void)buf;
  return n;
}

static int null_read(void *buf, int n) {
  (void)buf;
  (void)n;
  return 0;
}

void devsw_init(void) {
  devsw[CONSOLE_MAJOR].read = console_read;
  devsw[CONSOLE_MAJOR].write = console_write;
  devsw[NULL_MAJOR].read = null_read;
  devsw[NULL_MAJOR].write = null_write;
}
