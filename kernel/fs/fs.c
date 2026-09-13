/*
 * fs.c - the VFS glue: path walking, the global open-file table and the
 * per-process descriptor table.  The inode/content implementation lives in
 * memfs.c; the two are split the same way xv6 splits fs.c and file.c.
 */

#include "fs.h"
#include "string.h"
#include <stdint.h>

#define NFILE 64

int fs_errno = 0;

/* Positive errno for the syscall layer; EIO when an operation failed without
 * recording anything more specific. */
int fs_error(void) { return fs_errno ? fs_errno : EIO; }

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

int filetruncate(struct file *f, uint32_t size) {
  if (!f || !f->ip || f->type != FD_INODE) {
    fs_errno = EINVAL;
    return -1;
  }
  if (itruncate(f->ip, size) < 0) {
    fs_errno = EFBIG;
    return -1;
  }
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

/*
 * Current working directory, shared by all processes.  There is exactly one
 * user process (the shell) at a time and exec preserves it, so a global is
 * sufficient until per-process isolation lands.
 */
static char fs_cwd[256] = "/";

/* Lexically normalize an absolute path ("//", ".", ".."). */
static int path_normalize(const char *in, char *out, uint32_t outsz) {
  uint32_t n = 1;
  const char *p = in;

  if (outsz < 2)
    return -1;
  out[0] = '/';

  while (*p) {
    while (*p == '/')
      p++;
    if (!*p)
      break;
    const char *s = p;
    while (*p && *p != '/')
      p++;
    uint32_t len = (uint32_t)(p - s);

    if (len == 1 && s[0] == '.')
      continue;
    if (len == 2 && s[0] == '.' && s[1] == '.') {
      if (n > 1) {
        n--;
        while (n > 0 && out[n - 1] != '/')
          n--;
        if (n == 0)
          n = 1;
      }
      continue;
    }

    if (n + len + 1 >= outsz)
      return -1;
    kmemcpy(out + n, s, len);
    n += len;
    out[n++] = '/';
  }

  if (n > 1 && out[n - 1] == '/')
    n--;
  out[n] = 0;
  return 0;
}

/* Make a possibly-relative path absolute (joined with fs_cwd) and normalized. */
static int path_absolute(const char *path, char *out, uint32_t outsz) {
  char tmp[256];

  if (path[0] == '/') {
    if (kstrlen(path) >= sizeof(tmp))
      return -1;
    kstrcpy(tmp, path);
  } else {
    uint32_t cl = kstrlen(fs_cwd);
    uint32_t pl = kstrlen(path);
    if (cl + 1 + pl >= sizeof(tmp))
      return -1;
    kmemcpy(tmp, fs_cwd, cl);
    tmp[cl] = '/';
    kmemcpy(tmp + cl + 1, path, pl + 1);
  }
  return path_normalize(tmp, out, outsz);
}

/* Inode of the current working directory. */
static struct inode *iget_cwd(void) {
  struct inode *ip = iget(ROOTINO);
  char name[DIRSIZ];
  const char *p;

  if (!ip)
    return 0;
  p = skipelem(fs_cwd, name);
  while (p) {
    if (ip->type != T_DIR)
      return 0;
    uint32_t inum = (uint32_t)dirlookup(ip, name, 0);
    if (inum == 0)
      return 0;
    ip = iget(inum);
    if (!ip)
      return 0;
    p = skipelem(p, name);
  }
  return ip;
}

/*
 * Resolve `path`.  A relative path starts at `base` (NULL means the process
 * cwd); absolute paths always start at the root.  With `parent` set, walking
 * stops at the final component and returns its directory.
 */
static struct inode *namexat(struct inode *base, const char *path, int parent,
                             char *name) {
  struct inode *ip = (path[0] == '/') ? iget(ROOTINO)
                                      : (base ? base : iget_cwd());
  if (!ip) {
    fs_errno = ENOENT;
    return 0;
  }

  const char *p = skipelem(path, name);
  while (p) {
    if (ip->type != T_DIR) {
      fs_errno = ENOTDIR;
      return 0;
    }
    if (parent && *p == 0)
      return ip;
    uint32_t inum = (uint32_t)dirlookup(ip, name, 0);
    if (inum == 0) {
      fs_errno = ENOENT;
      return 0;
    }
    ip = iget(inum);
    if (!ip) {
      fs_errno = ENOENT;
      return 0;
    }
    p = skipelem(p, name);
  }
  if (parent) {
    fs_errno = ENOENT;
    return 0;
  }
  return ip;
}

struct inode *nameiat(struct inode *base, const char *path) {
  char name[DIRSIZ];
  return namexat(base, path, 0, name);
}

struct inode *nameiparentat(struct inode *base, const char *path, char *name) {
  return namexat(base, path, 1, name);
}

struct inode *namei(const char *path) { return nameiat(0, path); }

struct inode *nameiparent(const char *path, char *name) {
  return nameiparentat(0, path, name);
}

/* --- open -------------------------------------------------------------- */

struct file *vfs_open_at(struct inode *base, const char *path, int flags) {
  struct inode *ip;

  if (flags & O_CREAT) {
    char name[DIRSIZ];
    struct inode *dp = nameiparentat(base, path, name);
    if (!dp)
      return 0;
    uint32_t inum = (uint32_t)dirlookup(dp, name, 0);
    if (inum) {
      if (flags & O_EXCL) {
        fs_errno = EEXIST;
        return 0;
      }
      ip = iget(inum);
    } else {
      ip = ialloc(T_FILE, 0, 0);
      if (!ip) {
        fs_errno = ENOSPC;
        return 0;
      }
      if (dirlink(dp, name, ip->inum) < 0) {
        ifree(ip);
        fs_errno = ENOSPC;
        return 0;
      }
    }
  } else {
    ip = nameiat(base, path);
    if (!ip)
      return 0;
  }

  if ((flags & O_TRUNC) && ip->type == T_FILE) {
    if (itruncate(ip, 0) < 0) {
      fs_errno = EIO;
      return 0;
    }
  }

  struct file *f = filealloc();
  if (!f) {
    fs_errno = ENFILE;
    return 0;
  }
  f->type = (ip->type == T_DEVICE) ? FD_DEVICE : FD_INODE;
  f->ip = ip;
  f->readable = !(flags & O_WRONLY);
  f->writable = (flags & (O_WRONLY | O_RDWR)) != 0;
  f->off = (flags & O_APPEND) ? ip->size : 0;
  return f;
}

struct file *vfs_open(const char *path, int flags) {
  return vfs_open_at(0, path, flags);
}

int vfs_mkdir_at(struct inode *base, const char *path) {
  char name[DIRSIZ];
  struct inode *dp = nameiparentat(base, path, name);
  if (!dp)
    return -1;
  if (dirlookup(dp, name, 0)) {
    fs_errno = EEXIST;
    return -1;
  }
  struct inode *ip = ialloc(T_DIR, 0, 0);
  if (!ip) {
    fs_errno = ENOSPC;
    return -1;
  }
  ip->nlink = 2;
  if (dirlink(dp, name, ip->inum) < 0) {
    ifree(ip);
    fs_errno = ENOSPC;
    return -1;
  }
  dirlink(ip, ".", ip->inum);
  dirlink(ip, "..", dp->inum);
  dp->nlink++;
  return 0;
}

int vfs_mkdir(const char *path) { return vfs_mkdir_at(0, path); }

int vfs_unlink_at(struct inode *base, const char *path) {
  char name[DIRSIZ];
  struct inode *dp = nameiparentat(base, path, name);
  if (!dp)
    return -1;
  uint32_t inum = (uint32_t)dirlookup(dp, name, 0);
  if (!inum) {
    fs_errno = ENOENT;
    return -1;
  }
  struct inode *ip = iget(inum);
  if (ip && ip->type == T_DIR) {
    fs_errno = EISDIR;
    return -1;
  }
  dirunlink(dp, name);
  if (ip && --ip->nlink <= 0)
    ifree(ip);
  return 0;
}

int vfs_unlink(const char *path) { return vfs_unlink_at(0, path); }

int vfs_rmdir_at(struct inode *base, const char *path) {
  char name[DIRSIZ];
  struct inode *dp = nameiparentat(base, path, name);
  if (!dp)
    return -1;
  uint32_t inum = (uint32_t)dirlookup(dp, name, 0);
  if (!inum) {
    fs_errno = ENOENT;
    return -1;
  }
  struct inode *ip = iget(inum);
  if (!ip || ip->type != T_DIR) {
    fs_errno = ENOTDIR;
    return -1;
  }
  if (!isdirempty(ip)) {
    fs_errno = ENOTEMPTY;
    return -1;
  }
  dirunlink(dp, name);
  if (dp->nlink)
    dp->nlink--;
  ip->nlink = 0;
  ifree(ip);
  return 0;
}

int vfs_rmdir(const char *path) { return vfs_rmdir_at(0, path); }

int vfs_rename_at(struct inode *obase, const char *oldpath,
                  struct inode *nbase, const char *newpath) {
  char oname[DIRSIZ], nname[DIRSIZ];
  struct inode *odp = nameiparentat(obase, oldpath, oname);
  if (!odp)
    return -1;
  uint32_t inum = (uint32_t)dirlookup(odp, oname, 0);
  if (!inum) {
    fs_errno = ENOENT;
    return -1;
  }
  struct inode *ndp = nameiparentat(nbase, newpath, nname);
  if (!ndp)
    return -1;

  struct inode *ip = iget(inum);
  uint32_t existing = (uint32_t)dirlookup(ndp, nname, 0);
  if (existing == inum)
    return 0; /* same file, nothing to do */
  if (existing) {
    struct inode *eip = iget(existing);
    if (eip && eip->type == T_DIR) {
      fs_errno = EISDIR;
      return -1;
    }
    dirunlink(ndp, nname);
    if (eip && --eip->nlink <= 0)
      ifree(eip);
  }

  dirunlink(odp, oname);
  if (dirlink(ndp, nname, inum) < 0) {
    fs_errno = ENOSPC;
    return -1;
  }
  if (ip && ip->type == T_DIR && odp != ndp) {
    dirunlink(ip, "..");
    dirlink(ip, "..", ndp->inum);
    if (odp->nlink)
      odp->nlink--;
    ndp->nlink++;
  }
  return 0;
}

int vfs_rename(const char *oldpath, const char *newpath) {
  return vfs_rename_at(0, oldpath, 0, newpath);
}

int vfs_link_at(struct inode *obase, const char *oldpath,
                struct inode *nbase, const char *newpath) {
  struct inode *ip = nameiat(obase, oldpath);
  if (!ip)
    return -1;
  if (ip->type == T_DIR) {
    fs_errno = EPERM;
    return -1;
  }
  char name[DIRSIZ];
  struct inode *dp = nameiparentat(nbase, newpath, name);
  if (!dp)
    return -1;
  if (dirlookup(dp, name, 0)) {
    fs_errno = EEXIST;
    return -1;
  }
  if (dirlink(dp, name, ip->inum) < 0) {
    fs_errno = ENOSPC;
    return -1;
  }
  ip->nlink++;
  return 0;
}

int vfs_link(const char *oldpath, const char *newpath) {
  return vfs_link_at(0, oldpath, 0, newpath);
}

int vfs_chmod_at(struct inode *base, const char *path, uint16_t mode) {
  struct inode *ip = nameiat(base, path);
  if (!ip)
    return -1;
  ip->mode = mode & 07777;
  return 0;
}

int vfs_chmod(const char *path, uint16_t mode) {
  return vfs_chmod_at(0, path, mode);
}

int vfs_stat_at(struct inode *base, const char *path, struct stat *st) {
  struct inode *ip = nameiat(base, path);
  if (!ip)
    return -1;
  stati(ip, st);
  return 0;
}

int vfs_chdir(const char *path) {
  struct inode *ip = namei(path);
  if (!ip)
    return -1;
  if (ip->type != T_DIR) {
    fs_errno = ENOTDIR;
    return -1;
  }
  char abs[256];
  if (path_absolute(path, abs, sizeof(abs)) < 0) {
    fs_errno = ENAMETOOLONG;
    return -1;
  }
  kstrcpy(fs_cwd, abs);
  return 0;
}

int vfs_getcwd(char *buf, uint32_t size) {
  uint32_t n = kstrlen(fs_cwd) + 1;
  if (size < n) {
    fs_errno = ERANGE;
    return -1;
  }
  kmemcpy(buf, fs_cwd, n);
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
