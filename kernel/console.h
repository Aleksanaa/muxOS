#pragma once

char console_getchar(void);
/* Like console_getchar(), but reports whether the byte came from the PS/2
 * keyboard (as opposed to COM1), so callers can echo it locally. */
char console_getchar_src(int *from_kb);
