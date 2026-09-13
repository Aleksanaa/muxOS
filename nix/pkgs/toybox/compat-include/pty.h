/* muxOS stub for toybox's <pty.h> usage (openpty/forkpty are unused here). */
#ifndef MUXOS_PTY_H
#define MUXOS_PTY_H

#include <termios.h>
#include <sys/ioctl.h>

int openpty(int *amaster, int *aslave, char *name, const struct termios *termp,
            const struct winsize *winp);
int forkpty(int *amaster, char *name, const struct termios *termp,
            const struct winsize *winp);

#endif
