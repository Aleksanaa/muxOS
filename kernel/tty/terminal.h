#pragma once

/*
 * A tiny VT100/ANSI terminal emulator for the VGA text console.  The escape
 * parser is the vendored lw_terminal_parser (BSD); this module implements the
 * screen model and renders into the 80x25 colour text buffer.
 */

/* Console line discipline bits, set via tcsetattr and read by console_read. */
#define TTY_CANON 1
#define TTY_ECHO  2
#define TTY_ISIG  4

void terminal_init(void);
int terminal_ready(void);

/* Feed application output (which may contain escape sequences) to the screen. */
void terminal_write(const char *buf, int n);

/* Clear the screen and reset all terminal state. */
void terminal_reset(void);

/* Line discipline mode (TTY_* bits). */
int terminal_mode(void);
void terminal_set_mode(int mode);
