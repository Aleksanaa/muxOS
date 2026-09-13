/* Minimal compatibility shims for building toybox against mlibc on muxOS.
 *
 * toybox's lib/ is written for Linux/BSD and references a handful of types
 * and functions mlibc does not provide.  The Linux-specific parts are
 * intentionally left out; these declarations only let the (unused) portable
 * fallbacks compile, and --gc-sections drops them.
 */
#ifndef TOYBOX_MUXOS_COMPAT_H
#define TOYBOX_MUXOS_COMPAT_H

#include <errno.h>
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

/* No xattr support on muxOS.  toybox's portable wrappers (xattr_*) call the
 * Linux getxattr family directly, so map those calls to a stub.  The wrappers
 * themselves are defined by lib/portability.c. */
static inline long muxos_xattr_unsupported(void) {
  errno = ENOTSUP;
  return -1;
}
#define getxattr(...) muxos_xattr_unsupported()
#define lgetxattr(...) muxos_xattr_unsupported()
#define fgetxattr(...) muxos_xattr_unsupported()
#define listxattr(...) muxos_xattr_unsupported()
#define llistxattr(...) muxos_xattr_unsupported()
#define flistxattr(...) muxos_xattr_unsupported()
#define setxattr(...) muxos_xattr_unsupported()
#define lsetxattr(...) muxos_xattr_unsupported()
#define fsetxattr(...) muxos_xattr_unsupported()

ssize_t xattr_get(const char *path, const char *name, void *value, size_t size);
ssize_t xattr_lget(const char *path, const char *name, void *value, size_t size);
ssize_t xattr_fget(int fd, const char *name, void *value, size_t size);
ssize_t xattr_list(const char *path, char *list, size_t size);
ssize_t xattr_llist(const char *path, char *list, size_t size);
ssize_t xattr_flist(int fd, char *list, size_t size);
ssize_t xattr_set(const char *path, const char *name, const void *value,
                  size_t size, int flags);
ssize_t xattr_lset(const char *path, const char *name, const void *value,
                   size_t size, int flags);
ssize_t xattr_fset(int fd, const char *name, const void *value, size_t size,
                   int flags);

#ifndef ECHOCTL
#define ECHOCTL 0x200
#endif
#ifndef ECHOKE
#define ECHOKE 0x800
#endif

#endif
