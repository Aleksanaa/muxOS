/* muxOS stub for <sys/ioctl.h>: mlibc does not install this header, but the
 * ioctl() entry point and the terminal requests below are implemented. */
#ifndef MUXOS_SYS_IOCTL_H
#define MUXOS_SYS_IOCTL_H

#define TCGETS     0x5401
#define TCSETS     0x5402
#define TIOCGPGRP  0x540F
#define TIOCSPGRP  0x5410
#define TIOCSTI    0x5412
#define TIOCNOTTY  0x5422
#define TIOCSCTTY  0x540E
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414

int ioctl(int fd, unsigned long request, ...);

#endif
