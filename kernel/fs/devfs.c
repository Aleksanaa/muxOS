/*
 * devfs.c - trivial character devices behind the VFS.
 *
 * fd 0/1/2 point at /dev/console; writes go to the VGA console (which also
 * mirrors to the serial port), reads pull from the keyboard buffer.
 */

#include "console.h"
#include "fs.h"
#include "keyboard.h"
#include "process.h"
#include "serial.h"
#include "terminal.h"
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

/*
 * There is no tty line discipline, so do the minimal canonical-mode job here:
 * echo keyboard input, handle backspace/delete, and terminate on Enter.
 * COM1 input is not echoed because the connected terminal already does it.
 */
static int console_read(void *buf, int n) {
  asm volatile("sti");
  char *b = (char *)buf;
  int i = 0;
  int mode = terminal_mode();

  /* Raw mode (vi & friends): no echo, no line buffering, no editing.  Return
   * whatever is available after the first byte so read() doesn't block for a
   * whole line. */
  if (!(mode & TTY_CANON)) {
    while (i < n) {
      int from_kb = 0;
      char c = console_getchar_src(&from_kb);
      if (c == '\r')
        c = '\n'; /* ICRNL */
      if ((mode & TTY_ISIG) && c == '\x03') {
        int fg = foreground_pgid;
        process_kill(fg ? -fg : (int)processes[current].pid, SIGINT);
        if (processes[current].sig_handler[SIGINT] == 1)
          continue;
        return -EINTR;
      }
      b[i++] = c;
      if (!kb_haschar() && !serial_haschar())
        break;
    }
    return i;
  }

  /* Remember what is on this row before the cursor (the shell prompt) so ^L
   * can clear and redraw the line without losing it. */
  char prefix[80];
  int plen = terminal_snapshot_line(prefix, sizeof(prefix));

  while (i < n) {
    int from_kb = 0;
    char c = console_getchar_src(&from_kb);
    if (c == '\r')
      c = '\n';
    if (c == 0x0C) { /* ^L: clear screen, then repaint prompt + line so far */
      clear_screen();
      if (plen > 0)
        print(prefix, 0x07);
      for (int j = 0; j < i; j++) {
        char s[2] = {b[j], 0};
        print(s, 0x07);
      }
      continue;
    }
    if (c == '\x03') { /* ^C: interrupt the foreground group */
      int fg = foreground_pgid;
      process_kill(fg ? -fg : (int)processes[current].pid, SIGINT);
      if (processes[current].sig_handler[SIGINT] == 1)
        continue; /* the reader ignores SIGINT (e.g. the shell) */
      if (from_kb)
        print("^C\n", 0x07);
      return -EINTR;
    }
    if (c == 0x04) { /* ^D: EOF on an empty line */
      if (i == 0)
        return 0;
      continue;
    }
    if (c == '\n') {
      if (from_kb)
        print("\n", 0x07);
      b[i++] = '\n';
      break;
    }
    if (c == '\b' || c == 127) {
      if (i > 0) {
        i--;
        if (from_kb)
          print("\b \b", 0x07); /* erase through the terminal */
      }
      continue;
    }
    if (from_kb) {
      char s[2] = {c, 0};
      print(s, 0x07);
    }
    b[i++] = c;
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
