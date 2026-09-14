#ifndef FS_H
#define FS_H

#include "fs_uapi.h"
#include <stdint.h>

/*
 * muxOS in-memory filesystem.
 *
 * The design is borrowed from xv6 (MIT, Kaashoek/Morris/Cox):
 * an inode table, directory files containing dirents, readi/writei,
 * and a global open-file table.  The disk layer is replaced by the
 * physical page allocator: an inode's "blocks" are physical pages
 * that pmm_alloc() hands back already identity-mapped, so the kernel
 * can dereference them directly.  There is no logging, no buffer
 * cache, no on-disk format and (single CPU) no inode locks.
 */

#define BSIZE      4096u
/* 512 direct blocks = 2 MiB: big enough for a static toybox applet. */
#define NDIRECT    512u
#define MAXFILE    (NDIRECT * BSIZE)
#define MAX_INODES 128u

#define ROOTINO 1

#ifndef FD_MAX
#define FD_MAX 16
#endif

#define NDEV 4
#define CONSOLE_MAJOR 1
#define NULL_MAJOR 2

/* Which backend a struct inode is fulfilled by. */
#define INODE_MEM 0
#define INODE_9P 1

struct inode {
  uint32_t inum;
  int type;
  int major;
  int minor;
  int nlink;
  uint16_t mode; /* permission bits, see MODE_* in fs_uapi.h */
  uint32_t size;
  uint32_t addrs[NDIRECT];

  /* 9p-backed inode state.  `fid` is the 9P fid for this file; directories
   * keep theirs unopened (so it can be walked) and read a clone. */
  int backend;       /* INODE_MEM or INODE_9P */
  uint32_t fid;      /* 9P fid, NOFID when not yet associated */
  uint32_t parent;   /* inum of the parent directory (for "..") */
  int dir_loaded;    /* 9p directory listing has been fetched */
  int fid_opened;    /* 9P fid has been Topen'd for I/O */
};

/* Keep struct pipe within a single 4 KiB pmm page. */
#define PIPE_BUF_SIZE 2048

/* A unidirectional byte stream with reader/writer endpoint counts.  The buffer
 * is a ring; readers see EOF once all writers close and writers get EPIPE once
 * all readers close. */
struct pipe {
  char buf[PIPE_BUF_SIZE];
  uint32_t r;
  uint32_t w;
  uint32_t count;
  int readers;
  int writers;
};

struct file {
  enum { FD_NONE, FD_INODE, FD_DEVICE, FD_PIPE } type;
  int ref;
  int readable;
  int writable;
  struct inode *ip;
  uint32_t off;
  struct pipe *pipe; /* FD_PIPE only */
};

struct devsw {
  int (*read)(void *buf, int n);
  int (*write)(const void *buf, int n);
};

extern struct devsw devsw[];

/*
 * Last filesystem error, as a positive errno (0 means "no error yet").
 * VFS operations return -1/NULL on failure and record the reason here; the
 * syscall layer turns it into a negative errno for userspace.
 */
extern int fs_errno;
int fs_error(void);

/* memfs: inode + content layer */
void fs_init(void);
void fs_selftest(void);
void devsw_init(void);
struct inode *iget(uint32_t inum);
struct inode *ialloc(int type, int major, int minor);
void itrunc(struct inode *ip);
int itruncate(struct inode *ip, uint32_t size);
int isdirempty(struct inode *ip);
void ifree(struct inode *ip);
int readi(struct inode *ip, void *dst, uint32_t off, uint32_t n);
int writei(struct inode *ip, const void *src, uint32_t off, uint32_t n);
int dirlookup(struct inode *dp, const char *name, uint32_t *poff);
int dirlink(struct inode *dp, const char *name, uint32_t inum);
int dirunlink(struct inode *dp, const char *name);
void stati(struct inode *ip, struct stat *st);

/* VFS: paths, open file table, fd table */
void fileinit(void);
struct file *filealloc(void);
struct file *filedup(struct file *f);
void fileclose(struct file *f);
int fileread(struct file *f, void *buf, int n);
int filewrite(struct file *f, const void *buf, int n);
int filestat(struct file *f, struct stat *st);
int filetruncate(struct file *f, uint32_t size);
struct inode *namei(const char *path);
struct inode *nameiparent(const char *path, char *name);
struct inode *nameiat(struct inode *base, const char *path);
struct inode *nameiparentat(struct inode *base, const char *path, char *name);
struct file *vfs_open(const char *path, int flags);
struct file *vfs_open_at(struct inode *base, const char *path, int flags);
int vfs_mkdir(const char *path);
int vfs_mkdir_at(struct inode *base, const char *path);
int vfs_unlink(const char *path);
int vfs_unlink_at(struct inode *base, const char *path);
int vfs_rmdir(const char *path);
int vfs_rmdir_at(struct inode *base, const char *path);
int vfs_rename(const char *oldpath, const char *newpath);
int vfs_rename_at(struct inode *obase, const char *oldpath,
                  struct inode *nbase, const char *newpath);
int vfs_link(const char *oldpath, const char *newpath);
int vfs_link_at(struct inode *obase, const char *oldpath,
                struct inode *nbase, const char *newpath);
int vfs_chmod(const char *path, uint16_t mode);
int vfs_chmod_at(struct inode *base, const char *path, uint16_t mode);
int vfs_stat_at(struct inode *base, const char *path, struct stat *st);
int vfs_chdir(const char *path);
int vfs_getcwd(char *buf, uint32_t size);
int fdalloc(struct file **fds, struct file *f);
struct file *fdget(struct file **fds, int fd);
void fdclose(struct file **fds, int fd);
void fd_init(struct file **fds);
void fd_fork(struct file **parent, struct file **child);

/* Create a pipe and hand back its two endpoints (refcount 1 each). */
int vfs_pipe(struct file **readf, struct file **writef);

/* 9p-backed filesystem glue (9p_client.c). */
int v9p_mount(const char *path);
int v9p_loaddir(struct inode *ip);
int v9p_readi(struct inode *ip, void *dst, uint32_t off, uint32_t n);

#endif
