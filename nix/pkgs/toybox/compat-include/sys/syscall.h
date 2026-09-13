/* muxOS stub for <sys/syscall.h>.  muxOS does not expose raw Linux syscall
 * numbers; enough exists for the (unused) portable fallbacks to compile. */
#ifndef MUXOS_SYS_SYSCALL_H
#define MUXOS_SYS_SYSCALL_H

long syscall(long number, ...);

#endif
