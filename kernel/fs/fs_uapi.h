#ifndef FS_UAPI_H
#define FS_UAPI_H

/*
 * Kernel/user ABI for the muxOS filesystem.  Included by both the kernel
 * (kernel/fs/fs.h) and user programs (user/lib/userlib.h) so the struct
 * layouts and flag values cannot drift apart.
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
#define O_TRUNC   0x200
#define O_APPEND  0x400
#define O_ACCMODE 0x003

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define DIRSIZ 32u

struct dirent {
  uint32_t inum;
  uint16_t type;
  char name[DIRSIZ];
};

struct stat {
  uint32_t ino;
  uint16_t type;
  uint16_t nlink;
  uint32_t size;
  uint32_t dev;
};

#endif
