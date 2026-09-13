/*
 * fs.c - the VFS glue: path walking, the global open-file table and the
 * per-process descriptor table.  The inode/content implementation lives in
 * memfs.c; the two are split the same way xv6 splits fs.c and file.c.
 */

#include "fs.h"
#include "string.h"
#include <stdint.h>

#define NFILE 64

static struct file ftable[NFILE];

void fileinit(void) { kmemset(ftable, 0, sizeof(ftable)); }

struct file *filealloc(void) {
  for (int i = 0; i < NFILE; i++) {
    if (ftable[i].ref == 0) {
      kmemset(&ftable[i], 0, sizeof(ftable[i]));
      ftable[i].ref = 1;
      ftable[i].type = FD_NONE;
      return &ftable[i];
    }
  }
  return 0;
}

struct file *filedup(struct file *f) {
  if (f && f->ref >= 1)
    f->ref++;
  return f;
}

void fileclose(struct file *f) {
  if (!f || f->ref < 1)
    return;
  if (--f->ref > 0)
    return;
  if (f->ip && f->ip->nlink == 0)
    ifree(f->ip);
  f->type = FD_NONE;
  f->ip = 0;
  f->off = 0;
}

int fileread(struct file *f, void *buf, int n) {
  if (!f || !f->readable || n < 0)
    return -1;
  if (f->type == FD_DEVICE) {
    struct devsw *d = &devsw[f->ip->major];
    if (!d->read)
      return -1;
    return d->read(buf, n);
  }
  if (f->type == FD_INODE) {
    int r = readi(f->ip, buf, f->off, (uint32_t)n);
    if (r > 0)
      f->off += (uint32_t)r;
    return r;
  }
  return -1;
}

int filewrite(struct file *f, const void *buf, int n) {
  if (!f || !f->writable || n < 0)
    return -1;
  if (f->type == FD_DEVICE) {
    struct devsw *d = &devsw[f->ip->major];
    if (!d->write)
      return -1;
    return d->write(buf, n);
  }
  if (f->type == FD_INODE) {
    int r = writei(f->ip, buf, f->off, (uint32_t)n);
    if (r > 0)
      f->off += (uint32_t)r;
    return r;
  }
  return -1;
}

int filestat(struct file *f, struct stat *st) {
  if (!f || !f->ip)
    return -1;
  stati(f->ip, st);
  return 0;
}

/* --- paths ------------------------------------------------------------- */

static const char *skipelem(const char *path, char *name) {
  while (*path == '/')
    path++;
  if (*path == 0)
    return 0;
  const char *s = path;
  while (*path != '/' && *path != 0)
    path++;
  uint32_t len = (uint32_t)(path - s);
  if (len >= DIRSIZ)
    len = DIRSIZ - 1;
  kmemcpy(name, s, len);
  name[len] = 0;
  while (*path == '/')
    path++;
  return path;
}

static struct inode *namex(const char *path, int parent, char *name) {
  struct inode *ip = iget(ROOTINO);
  if (!ip)
    return 0;
  const char *p = skipelem(path, name);
  while (p) {
    if (ip->type != T_DIR)
      return 0;
    if (parent && *p == 0)
      return ip;
    uint32_t inum = (uint32_t)dirlookup(ip, name, 0);
    if (inum == 0)
      return 0;
    ip = iget(inum);
    if (!ip)
      return 0;
    p = skipelem(p, name);
  }
  if (parent)
    return 0;
  return ip;
}

struct inode *namei(const char *path) {
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode *nameiparent(const char *path, char *name) {
  return namex(path, 1, name);
}

/* --- open -------------------------------------------------------------- */

struct file *vfs_open(const char *path, int flags) {
  struct inode *ip;

  if (flags & O_CREAT) {
    char name[DIRSIZ];
    struct inode *dp = nameiparent(path, name);
    if (!dp)
      return 0;
    uint32_t inum = (uint32_t)dirlookup(dp, name, 0);
    if (inum) {
      ip = iget(inum);
    } else {
      ip = ialloc(T_FILE, 0, 0);
      if (!ip || dirlink(dp, name, ip->inum) < 0) {
        if (ip)
          ifree(ip);
        return 0;
      }
    }
  } else {
    ip = namei(path);
    if (!ip)
      return 0;
  }

  if ((flags & O_TRUNC) && ip->type == T_FILE)
    itrunc(ip);

  struct file *f = filealloc();
  if (!f)
    return 0;
  f->type = (ip->type == T_DEVICE) ? FD_DEVICE : FD_INODE;
  f->ip = ip;
  f->readable = !(flags & O_WRONLY);
  f->writable = (flags & (O_WRONLY | O_RDWR)) != 0;
  f->off = (flags & O_APPEND) ? ip->size : 0;
  return f;
}

int vfs_mkdir(const char *path) {
  char name[DIRSIZ];
  struct inode *dp = nameiparent(path, name);
  if (!dp || dirlookup(dp, name, 0))
    return -1;
  struct inode *ip = ialloc(T_DIR, 0, 0);
  if (!ip)
    return -1;
  ip->nlink = 2;
  if (dirlink(dp, name, ip->inum) < 0) {
    ifree(ip);
    return -1;
  }
  dirlink(ip, ".", ip->inum);
  dirlink(ip, "..", dp->inum);
  dp->nlink++;
  return 0;
}

int vfs_unlink(const char *path) {
  char name[DIRSIZ];
  struct inode *dp = nameiparent(path, name);
  if (!dp)
    return -1;
  int inum = dirunlink(dp, name);
  if (inum < 0)
    return -1;
  struct inode *ip = iget((uint32_t)inum);
  if (!ip)
    return -1;
  if (ip->type == T_DIR)
    dp->nlink--;
  ifree(ip);
  return 0;
}

/* --- per-process fd table --------------------------------------------- */

int fdalloc(struct file **fds, struct file *f) {
  for (int fd = 0; fd < FD_MAX; fd++) {
    if (fds[fd] == 0) {
      fds[fd] = f;
      return fd;
    }
  }
  return -1;
}

struct file *fdget(struct file **fds, int fd) {
  if (fd < 0 || fd >= FD_MAX)
    return 0;
  return fds[fd];
}

void fdclose(struct file **fds, int fd) {
  if (fd < 0 || fd >= FD_MAX || !fds[fd])
    return;
  fileclose(fds[fd]);
  fds[fd] = 0;
}

void fd_init(struct file **fds) {
  for (int i = 0; i < FD_MAX; i++)
    fds[i] = 0;
  fds[0] = vfs_open("/dev/console", O_RDONLY);
  fds[1] = vfs_open("/dev/console", O_WRONLY);
  fds[2] = vfs_open("/dev/console", O_WRONLY);
}

void fd_fork(struct file **parent, struct file **child) {
  for (int i = 0; i < FD_MAX; i++)
    child[i] = parent[i] ? filedup(parent[i]) : 0;
}
