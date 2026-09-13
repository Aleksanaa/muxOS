/* Minimal compatibility shims for building toybox against mlibc on muxOS.
 *
 * toybox's lib/ is written for Linux/BSD and references a handful of types
 * and functions mlibc does not provide.  The Linux-specific parts are
 * intentionally left out; these declarations only let the (unused) portable
 * fallbacks compile, and --gc-sections drops them.
 */
#ifndef TOYBOX_MUXOS_COMPAT_H
#define TOYBOX_MUXOS_COMPAT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>
#include <termios.h>

struct statfs {
  unsigned long f_type;
  unsigned long f_bsize;
  unsigned long f_blocks;
  unsigned long f_bfree;
  unsigned long f_bavail;
  unsigned long f_files;
  unsigned long f_ffree;
  unsigned long f_fsid;
  unsigned long f_namelen;
  unsigned long f_frsize;
  unsigned long f_flags;
  unsigned long f_spare[4];
};

long syscall(long number, ...);

/* Present in glibc but not (yet) exposed by mlibc. */
char *strptime(const char *s, const char *format, struct tm *tm);
int wcwidth(wchar_t wc);
int cfsetspeed(struct termios *termios_p, speed_t speed);

/* Linux xattr API; toybox's wrappers are unused here. */
ssize_t getxattr(const char *path, const char *name, void *value, size_t size);
ssize_t lgetxattr(const char *path, const char *name, void *value, size_t size);
ssize_t fgetxattr(int fd, const char *name, void *value, size_t size);
ssize_t listxattr(const char *path, char *list, size_t size);
ssize_t llistxattr(const char *path, char *list, size_t size);
ssize_t flistxattr(int fd, char *list, size_t size);
int setxattr(const char *path, const char *name, const void *value, size_t size,
             int flags);
int lsetxattr(const char *path, const char *name, const void *value,
              size_t size, int flags);
int fsetxattr(int fd, const char *name, const void *value, size_t size,
              int flags);

#ifndef ECHOCTL
#define ECHOCTL 0x200
#endif
#ifndef ECHOKE
#define ECHOKE 0x800
#endif

#endif
