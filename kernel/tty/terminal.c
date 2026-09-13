/*
 * terminal.c - minimal ANSI/VT100 terminal emulator over the VGA text console.
 *
 * Escape parsing is delegated to the vendored lw_terminal_parser; this file
 * owns the screen (the VGA text buffer itself), cursor, scroll region,
 * attributes and the few control sequences neatvi and friends need.
 */

#include "terminal.h"
#include "io.h"
#include "lw_terminal_parser.h"

#include <stdint.h>

#define COLS 80
#define ROWS 25

#define ATTR_DEFAULT 0x07 /* light grey on black */

/* Default attribute a blank cell gets; follows the current background. */
static volatile uint16_t *const vga = (volatile uint16_t *)0xB8000;

static struct lw_terminal *parser;
static int ready;

static int cx, cy;          /* cursor */
static int top, bot;        /* scroll region, inclusive */
static int sx, sy;          /* saved cursor (DECSC) */
static int pending_wrap;    /* last column written, wrap on next char */
static int autowrap = 1;
static int cursor_visible = 1;

static int fg = 7, bg = 0;  /* 0..15 foreground, 0..7 background */
static int bold, reverse;

static int lflag = TTY_CANON | TTY_ECHO | TTY_ISIG;

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

static uint8_t attr(void) {
  int f = fg & 0x0F;
  int b = bg & 0x07;
  if (bold && f < 8)
    f |= 8;
  if (reverse) {
    int t = f;
    f = b;
    b = t;
  }
  return (uint8_t)((b << 4) | (f & 0x0F));
}

static void putcell(int x, int y, uint8_t c) {
  vga[y * COLS + x] = (uint16_t)(attr() << 8) | (uint8_t)c;
}

static void blank_cell(int x, int y) { putcell(x, y, ' '); }

static void blank_row(int y) {
  for (int x = 0; x < COLS; x++)
    blank_cell(x, y);
}

static void set_cursor(void) {
  if (!cursor_visible) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20); /* disable hardware cursor */
    return;
  }
  outb(0x3D4, 0x0A);
  outb(0x3D5, 0x0E);
  outb(0x3D4, 0x0B);
  outb(0x3D5, 0x0F);
  uint16_t pos = (uint16_t)(cy * COLS + cx);
  outb(0x3D4, 0x0E);
  outb(0x3D5, (uint8_t)(pos >> 8));
  outb(0x3D4, 0x0F);
  outb(0x3D5, (uint8_t)(pos & 0xFF));
}

/* Copy whole rows (handles overlap correctly by always copying up or down). */
static void move_rows(int dst, int src) {
  for (int x = 0; x < COLS; x++)
    vga[dst * COLS + x] = vga[src * COLS + x];
}

static void scroll_up(void) {
  for (int y = top; y < bot; y++)
    move_rows(y, y + 1);
  blank_row(bot);
}

static void scroll_down(void) {
  for (int y = bot; y > top; y--)
    move_rows(y, y - 1);
  blank_row(top);
}

static void linefeed(void) {
  if (cy >= bot)
    scroll_up();
  else if (cy < ROWS - 1)
    cy++;
}

static void putc(char c) {
  if (pending_wrap) {
    pending_wrap = 0;
    cx = 0;
    linefeed();
  }
  putcell(cx, cy, (uint8_t)c);
  if (cx == COLS - 1) {
    if (autowrap)
      pending_wrap = 1;
  } else {
    cx++;
  }
  set_cursor();
}

static void term_write_cb(struct lw_terminal *t, char c) {
  (void)t;
  unsigned char u = (unsigned char)c;
  switch (u) {
  case '\n': /* ONLCR: LF implies CR */
    cx = 0;
    pending_wrap = 0;
    linefeed();
    break;
  case '\r':
    cx = 0;
    pending_wrap = 0;
    break;
  case '\b':
    if (cx > 0)
      cx--;
    pending_wrap = 0;
    break;
  case '\t':
    do {
      putc(' ');
    } while ((cx % 8) != 0 && cx > 0);
    break;
  case '\a':
    break;
  case 0x0e: /* SO */
  case 0x0f: /* SI */
    break;
  default:
    if (u >= 32)
      putc((char)u);
    break;
  }
  set_cursor();
}

static int arg(struct lw_terminal *t, unsigned int i, int def) {
  if (i < t->argc && t->argv[i] != 0)
    return (int)t->argv[i];
  return def;
}

static void cursor_home(void) {
  cx = 0;
  cy = 0;
  pending_wrap = 0;
}

static void erase_line(int y, int from, int to) {
  if (from < 0)
    from = 0;
  if (to > COLS - 1)
    to = COLS - 1;
  for (int x = from; x <= to; x++)
    blank_cell(x, y);
}

static void erase_display(int mode) {
  if (mode == 0) {
    erase_line(cy, cx, COLS - 1);
    for (int y = cy + 1; y < ROWS; y++)
      blank_row(y);
  } else if (mode == 1) {
    for (int y = 0; y < cy; y++)
      blank_row(y);
    erase_line(cy, 0, cx);
  } else {
    for (int y = 0; y < ROWS; y++)
      blank_row(y);
  }
}

static void insert_lines(int n) {
  int limit = bot - cy + 1;
  if (n > limit)
    n = limit;
  for (int y = bot; y >= cy + n; y--)
    move_rows(y, y - n);
  for (int y = cy; y < cy + n; y++)
    blank_row(y);
}

static void delete_lines(int n) {
  int limit = bot - cy + 1;
  if (n > limit)
    n = limit;
  for (int y = cy; y <= bot - n; y++)
    move_rows(y, y + n);
  for (int y = bot - n + 1; y <= bot; y++)
    blank_row(y);
}

/* ---- CSI ---- */
static void csi_cuu(struct lw_terminal *t) { cy = imax(top, cy - arg(t, 0, 1)); }
static void csi_cud(struct lw_terminal *t) { cy = imin(bot, cy + arg(t, 0, 1)); }
static void csi_cuf(struct lw_terminal *t) { cx = imin(COLS - 1, cx + arg(t, 0, 1)); }
static void csi_cub(struct lw_terminal *t) { cx = imax(0, cx - arg(t, 0, 1)); }
static void csi_cnl(struct lw_terminal *t) { cx = 0; cy = imin(bot, cy + arg(t, 0, 1)); }
static void csi_cpl(struct lw_terminal *t) { cx = 0; cy = imax(top, cy - arg(t, 0, 1)); }
static void csi_cha(struct lw_terminal *t) { cx = imin(COLS - 1, arg(t, 0, 1) - 1); }
static void csi_vpa(struct lw_terminal *t) { cy = imin(ROWS - 1, arg(t, 0, 1) - 1); }
static void csi_cup(struct lw_terminal *t) {
  cy = imin(ROWS - 1, arg(t, 0, 1) - 1);
  cx = imin(COLS - 1, arg(t, 1, 1) - 1);
  pending_wrap = 0;
}
static void csi_ed(struct lw_terminal *t) { erase_display(arg(t, 0, 0)); }
static void csi_el(struct lw_terminal *t) {
  int m = arg(t, 0, 0);
  if (m == 0)
    erase_line(cy, cx, COLS - 1);
  else if (m == 1)
    erase_line(cy, 0, cx);
  else
    erase_line(cy, 0, COLS - 1);
}
static void csi_il(struct lw_terminal *t) { insert_lines(arg(t, 0, 1)); }
static void csi_dl(struct lw_terminal *t) { delete_lines(arg(t, 0, 1)); }
static void csi_su(struct lw_terminal *t) { for (int i = 0; i < arg(t, 0, 1); i++) scroll_up(); }
static void csi_sd(struct lw_terminal *t) { for (int i = 0; i < arg(t, 0, 1); i++) scroll_down(); }
static void csi_decstbm(struct lw_terminal *t) {
  int nt = arg(t, 0, 1) - 1;
  int nb = arg(t, 1, ROWS) - 1;
  if (nt < 0)
    nt = 0;
  if (nb > ROWS - 1)
    nb = ROWS - 1;
  if (nt >= nb)
    return;
  top = nt;
  bot = nb;
  cursor_home();
}

static void sgr(struct lw_terminal *t) {
  if (t->argc == 0) {
    bold = reverse = 0;
    fg = 7;
    bg = 0;
    return;
  }
  for (unsigned int i = 0; i < t->argc; i++) {
    int a = (int)t->argv[i];
    if (a == 0) {
      bold = reverse = 0;
      fg = 7;
      bg = 0;
    } else if (a == 1) {
      bold = 1;
    } else if (a == 22) {
      bold = 0;
    } else if (a == 7) {
      reverse = 1;
    } else if (a == 27) {
      reverse = 0;
    } else if (a >= 30 && a <= 37) {
      fg = a - 30;
    } else if (a == 39) {
      fg = 7;
    } else if (a >= 40 && a <= 47) {
      bg = a - 40;
    } else if (a == 49) {
      bg = 0;
    } else if (a >= 90 && a <= 97) {
      fg = a - 90 + 8;
    } else if (a >= 100 && a <= 107) {
      bg = a - 100;
    } else if (a == 38 || a == 48) {
      /* 38;5;N / 48;5;N (256-colour) or 38;2;r;g;b.  Approximate. */
      if (i + 1 < t->argc && t->argv[i + 1] == 5 && i + 2 < t->argc) {
        int idx = (int)t->argv[i + 2] & 0x0F;
        if (a == 38)
          fg = idx;
        else
          bg = idx & 0x07;
        i += 2;
      } else if (i + 1 < t->argc && t->argv[i + 1] == 2 && i + 4 < t->argc) {
        i += 4;
      }
    }
  }
}

static void csi_sm(struct lw_terminal *t) {
  for (unsigned int i = 0; i < t->argc; i++) {
    int m = (int)t->argv[i];
    if (t->flag == '?') {
      if (m == 25)
        cursor_visible = 1;
      else if (m == 7)
        autowrap = 1;
    }
  }
  set_cursor();
}

static void csi_rm(struct lw_terminal *t) {
  for (unsigned int i = 0; i < t->argc; i++) {
    int m = (int)t->argv[i];
    if (t->flag == '?') {
      if (m == 25)
        cursor_visible = 0;
      else if (m == 7)
        autowrap = 0;
    }
  }
  set_cursor();
}

/* ---- ESC ---- */
static void esc_ind(struct lw_terminal *t) { (void)t; linefeed(); }
static void esc_ri(struct lw_terminal *t) {
  (void)t;
  if (cy <= top)
    scroll_down();
  else
    cy--;
}
static void esc_nel(struct lw_terminal *t) {
  (void)t;
  cx = 0;
  linefeed();
}
static void esc_decsc(struct lw_terminal *t) {
  (void)t;
  sx = cx;
  sy = cy;
}
static void esc_decrc(struct lw_terminal *t) {
  (void)t;
  cx = sx;
  cy = sy;
  pending_wrap = 0;
}
static void esc_ris(struct lw_terminal *t) {
  (void)t;
  terminal_reset();
}

void terminal_reset(void) {
  cx = cy = 0;
  top = 0;
  bot = ROWS - 1;
  sx = sy = 0;
  pending_wrap = 0;
  autowrap = 1;
  cursor_visible = 1;
  bold = reverse = 0;
  fg = 7;
  bg = 0;
  for (int y = 0; y < ROWS; y++)
    blank_row(y);
  set_cursor();
}

void terminal_init(void) {
  parser = lw_terminal_parser_init();
  parser->write = term_write_cb;
  parser->callbacks.csi.A = csi_cuu;
  parser->callbacks.csi.B = csi_cud;
  parser->callbacks.csi.C = csi_cuf;
  parser->callbacks.csi.D = csi_cub;
  parser->callbacks.csi.E = csi_cnl;
  parser->callbacks.csi.F = csi_cpl;
  parser->callbacks.csi.G = csi_cha;
  parser->callbacks.csi.H = csi_cup;
  parser->callbacks.csi.f = csi_cup;
  parser->callbacks.csi.d = csi_vpa;
  parser->callbacks.csi.J = csi_ed;
  parser->callbacks.csi.K = csi_el;
  parser->callbacks.csi.L = csi_il;
  parser->callbacks.csi.M = csi_dl;
  parser->callbacks.csi.S = csi_su;
  parser->callbacks.csi.T = csi_sd;
  parser->callbacks.csi.r = csi_decstbm;
  parser->callbacks.csi.m = sgr;
  parser->callbacks.csi.h = csi_sm;
  parser->callbacks.csi.l = csi_rm;
  parser->callbacks.esc.D = esc_ind;
  parser->callbacks.esc.M = esc_ri;
  parser->callbacks.esc.E = esc_nel;
  parser->callbacks.esc.n7 = esc_decsc;
  parser->callbacks.esc.n8 = esc_decrc;
  parser->callbacks.esc.c = esc_ris;
  ready = 1;
  terminal_reset();
}

int terminal_ready(void) { return ready; }

void terminal_write(const char *buf, int n) {
  if (!ready)
    return;
  for (int i = 0; i < n; i++)
    lw_terminal_parser_read(parser, buf[i]);
}

int terminal_mode(void) { return lflag; }

void terminal_set_mode(int mode) { lflag = mode; }
