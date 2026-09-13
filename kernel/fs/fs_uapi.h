#ifndef FS_UAPI_H
#define FS_UAPI_H

/*
 * Kernel/user ABI for the muxOS filesystem.  Included by both the kernel
 * (kernel/fs/fs.h) and user programs (user/lib/userlib.h) so the struct
 * layouts and flag values cannot drift apart.
 *
 * The mlibc port cannot include this header (it defines libc's own
 * `struct stat`/`struct dirent`), so it keeps a copy of the two struct
 * layouts in ports/mlibc/sysdeps/muxos/sysdeps.cpp.  Keep them in sync.
 */

#include <stdint.h>

enum {
  T_DIR = 1,
  T_FILE = 2,
  T_DEVICE = 3,
};

#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREAT   0x040
#define O_EXCL    0x080
#define O_TRUNC   0x200
#define O_APPEND  0x400
#define O_ACCMODE 0x003

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define DIRSIZ 32u

/* Permission bits stored by the kernel; S_IF* bits are libc's business. */
#define MODE_DIR  0755
#define MODE_FILE 0644
#define MODE_DEV  0600

/*
 * Errno values shared by the kernel and the libc sysdeps.  These are the
 * Linux numbers, matching mlibc's abi-bits/errno.h.
 */
#define EPERM        1
#define ENOENT       2
#define ESRCH        3
#define EINTR        4
#define EIO          5
#define ENOEXEC      8
#define EBADF        9
#define ECHILD      10
#define EAGAIN      11
#define ENOMEM      12
#define EACCES      13
#define EEXIST      17
#define ENODEV      19
#define ENFILE      23
#define EMFILE      24
#define ENOTDIR     20
#define EISDIR      21
#define EINVAL      22
#define ENOTTY      25
#define EFBIG       27
#define ENOSPC      28
#define ESPIPE      29
#define EROFS       30
#define EPIPE       32
#define ERANGE      34
#define ENAMETOOLONG 36
#define ENOSYS      38
#define ENOTEMPTY   39

/* Directory entry as produced by getdents().  `off` is the offset just past
 * this entry, so lseek()+getdents() can resume after it. */
struct dirent {
  uint32_t inum;
  uint32_t off;
  uint32_t type;
  char name[DIRSIZ];
};

struct stat {
  uint32_t ino;
  uint16_t type;
  uint16_t mode;
  uint32_t nlink;
  uint32_t size;
  uint32_t dev;
};

#endif
