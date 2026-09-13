/*
 * memfs.c - in-memory inode/content backend.
 *
 * Adapted from xv6's fs.c (MIT License, Copyright (c) 2006-2024 Frans
 * Kaashoek, Robert Morris, Russ Cox, MIT).  The block device, buffer cache,
 * journal, superblock and on-disk inode table are gone: an inode's data
 * "blocks" are simply physical pages from pmm_alloc(), which vmm_init()
 * identity-maps, so they can be dereferenced as normal pointers.
 */

#include "fs.h"
#include "pmm.h"
#include "programs.h"
#include "string.h"
#include "vga.h"
#include <stdint.h>

/* One inode per slot, indexed by inum; inum 0 means "unused". */
static struct inode inode_table[MAX_INODES];

struct inode *iget(uint32_t inum) {
  if (inum == 0 || inum >= MAX_INODES)
    return 0;
  struct inode *ip = &inode_table[inum];
  if (ip->type == 0)
    return 0;
  return ip;
}

struct inode *ialloc(int type, int major, int minor) {
  for (uint32_t i = 1; i < MAX_INODES; i++) {
    if (inode_table[i].type == 0) {
      struct inode *ip = &inode_table[i];
      kmemset(ip, 0, sizeof(*ip));
      ip->inum = i;
      ip->type = type;
      ip->major = major;
      ip->minor = minor;
      ip->nlink = 1;
      ip->mode = (type == T_DIR) ? MODE_DIR
               : (type == T_DEVICE) ? MODE_DEV
                                    : MODE_FILE;
      return ip;
    }
  }
  return 0;
}

void itrunc(struct inode *ip) {
  for (uint32_t i = 0; i < NDIRECT; i++) {
    if (ip->addrs[i]) {
      pmm_free(ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }
  ip->size = 0;
}

void ifree(struct inode *ip) {
  itrunc(ip);
  ip->type = 0;
  ip->nlink = 0;
}

/* Return the physical page backing byte-block bn, allocating/zeroing it. */
static uint32_t bmap(struct inode *ip, uint32_t bn) {
  if (bn >= NDIRECT)
    return 0;
  if (ip->addrs[bn] == 0) {
    uint32_t page = pmm_alloc();
    if (!page)
      return 0;
    kmemset((void *)(uintptr_t)page, 0, BSIZE);
    ip->addrs[bn] = page;
  }
  return ip->addrs[bn];
}

int readi(struct inode *ip, void *dst, uint32_t off, uint32_t n) {
  if (off > ip->size)
    return 0;
  if (off + n > ip->size)
    n = ip->size - off;

  uint32_t tot = 0;
  while (tot < n) {
    uint32_t bn = (off + tot) / BSIZE;
    uint32_t bo = (off + tot) % BSIZE;
    uint32_t m = BSIZE - bo;
    if (m > n - tot)
      m = n - tot;
    uint32_t addr = ip->addrs[bn];
    if (addr == 0)
      break;
    kmemcpy((uint8_t *)dst + tot, (void *)(uintptr_t)(addr + bo), m);
    tot += m;
  }
  return (int)tot;
}

int writei(struct inode *ip, const void *src, uint32_t off, uint32_t n) {
  if (off > ip->size || off + n > MAXFILE)
    return -1;

  uint32_t tot = 0;
  while (tot < n) {
    uint32_t bn = (off + tot) / BSIZE;
    uint32_t bo = (off + tot) % BSIZE;
    uint32_t m = BSIZE - bo;
    if (m > n - tot)
      m = n - tot;
    uint32_t addr = bmap(ip, bn);
    if (addr == 0)
      break;
    kmemcpy((void *)(uintptr_t)(addr + bo), (const uint8_t *)src + tot, m);
    tot += m;
  }
  if (off + tot > ip->size)
    ip->size = off + tot;
  return (int)tot;
}

/*
 * Resize an inode to `size` bytes: growing allocates and zeroes the newly
 * exposed range, shrinking frees the whole blocks that fall off the end.
 */
int itruncate(struct inode *ip, uint32_t size) {
  if (size > MAXFILE)
    return -1;
  if (size < ip->size) {
    uint32_t first = (size + BSIZE - 1) / BSIZE;
    for (uint32_t bn = first; bn < NDIRECT; bn++) {
      if (ip->addrs[bn]) {
        pmm_free(ip->addrs[bn]);
        ip->addrs[bn] = 0;
      }
    }
  } else if (size > ip->size) {
    for (uint32_t off = ip->size; off < size;) {
      uint32_t addr = bmap(ip, off / BSIZE);
      if (addr == 0)
        return -1;
      uint32_t bo = off % BSIZE;
      uint32_t span = BSIZE - bo;
      if (span > size - off)
        span = size - off;
      kmemset((void *)(uintptr_t)(addr + bo), 0, span);
      off += span;
    }
  }
  ip->size = size;
  return 0;
}

static int namecmp(const char *a, const char *b) {
  return kstrncmp(a, b, DIRSIZ);
}

int dirlookup(struct inode *dp, const char *name, uint32_t *poff) {
  struct dirent de;
  for (uint32_t off = 0; off < dp->size; off += sizeof(de)) {
    if (readi(dp, &de, off, sizeof(de)) != (int)sizeof(de))
      return 0;
    if (de.inum == 0)
      continue;
    if (namecmp(name, de.name) == 0) {
      if (poff)
        *poff = off;
      return (int)de.inum;
    }
  }
  return 0;
}

int dirlink(struct inode *dp, const char *name, uint32_t inum) {
  if (dirlookup(dp, name, 0))
    return -1;

  struct dirent de;
  uint32_t off = 0;
  for (; off < dp->size; off += sizeof(de)) {
    if (readi(dp, &de, off, sizeof(de)) != (int)sizeof(de))
      return -1;
    if (de.inum == 0)
      break;
  }

  kmemset(&de, 0, sizeof(de));
  de.inum = inum;
  struct inode *target = iget(inum);
  de.type = target ? (uint16_t)target->type : 0;
  uint32_t i = 0;
  while (name[i] && i < DIRSIZ - 1) {
    de.name[i] = name[i];
    i++;
  }
  de.name[i] = 0;

  if (writei(dp, &de, off, sizeof(de)) != (int)sizeof(de))
    return -1;
  return 0;
}

int dirunlink(struct inode *dp, const char *name) {
  uint32_t off;
  uint32_t inum = (uint32_t)dirlookup(dp, name, &off);
  if (inum == 0)
    return -1;
  struct dirent de;
  kmemset(&de, 0, sizeof(de));
  if (writei(dp, &de, off, sizeof(de)) != (int)sizeof(de))
    return -1;
  return (int)inum;
}

/* A directory is empty when it contains nothing but "." and "..". */
int isdirempty(struct inode *ip) {
  struct dirent de;
  for (uint32_t off = 0; off < ip->size; off += sizeof(de)) {
    if (readi(ip, &de, off, sizeof(de)) != (int)sizeof(de))
      break;
    if (de.inum == 0)
      continue;
    if (kstrcmp(de.name, ".") == 0 || kstrcmp(de.name, "..") == 0)
      continue;
    return 0;
  }
  return 1;
}

void stati(struct inode *ip, struct stat *st) {
  st->ino = ip->inum;
  st->type = (uint16_t)ip->type;
  st->mode = ip->mode;
  st->nlink = (uint16_t)ip->nlink;
  st->size = ip->size;
  st->dev = 0;
}

static struct inode *create(struct inode *dir, const char *name, int type,
                            int major) {
  struct inode *ip = ialloc(type, major, 0);
  if (!ip)
    return 0;
  if (dirlink(dir, name, ip->inum) < 0) {
    ifree(ip);
    return 0;
  }
  return ip;
}

void fs_init(void) {
  kmemset(inode_table, 0, sizeof(inode_table));
  fileinit();
  devsw_init();

  /* Root inode is pinned to ROOTINO. */
  struct inode *root = &inode_table[ROOTINO];
  root->inum = ROOTINO;
  root->type = T_DIR;
  root->nlink = 2;
  root->mode = MODE_DIR;

  dirlink(root, ".", ROOTINO);
  dirlink(root, "..", ROOTINO);

  struct inode *dev = create(root, "dev", T_DIR, 0);
  if (dev) {
    dev->nlink = 2;
    dirlink(dev, ".", dev->inum);
    dirlink(dev, "..", ROOTINO);
    root->nlink++;
    create(dev, "console", T_DEVICE, CONSOLE_MAJOR);
    create(dev, "tty", T_DEVICE, CONSOLE_MAJOR);
    create(dev, "null", T_DEVICE, NULL_MAJOR);
  }

  struct inode *hello = create(root, "hello", T_FILE, 0);
  if (hello) {
    const char *msg = "Hello from the muxOS memfs!\n";
    writei(hello, msg, 0, kstrlen(msg));
  }

  /* Copy every embedded program into /bin so the shell can exec it. */
  struct inode *bin = create(root, "bin", T_DIR, 0);
  if (bin) {
    bin->nlink = 2;
    dirlink(bin, ".", bin->inum);
    dirlink(bin, "..", ROOTINO);
    root->nlink++;
    for (uint32_t i = 0; i < embedded_program_count; i++) {
      const struct embedded_program *p = &embedded_programs[i];
      uint32_t size = (uint32_t)(p->end - p->start);
      struct inode *f = create(bin, p->name, T_FILE, 0);
      if (!f || size == 0)
        continue;
      if (size > MAXFILE)
        size = MAXFILE;
      writei(f, p->start, 0, size);
    }
  }

  print("[OK] FS init\n", 0);
}

/*
 * Boot-time smoke test: exercises create/write/read/compare, mkdir + stat and
 * unlink through the public VFS entry points.  Prints to the console (which
 * also mirrors to COM1).
 */
void fs_selftest(void) {
  const char *path = "/selftest";
  const char *payload = "muxOS memfs payload 12345";

  struct file *f = vfs_open(path, O_CREAT | O_RDWR | O_TRUNC);
  if (!f) {
    print("[FS] selftest: create failed\n", 0x0C);
    return;
  }
  filewrite(f, payload, (int)kstrlen(payload));
  fileclose(f);

  f = vfs_open(path, O_RDONLY);
  if (!f) {
    print("[FS] selftest: open failed\n", 0x0C);
    return;
  }
  char buf[64];
  int n = fileread(f, buf, sizeof(buf) - 1);
  fileclose(f);
  if (n < 0)
    n = 0;
  buf[n] = 0;
  int rw_ok = kstrcmp(buf, payload) == 0;

  int dir_ok = 0;
  vfs_mkdir("/selftest_dir");
  struct inode *ip = namei("/selftest_dir");
  if (ip) {
    struct stat st;
    stati(ip, &st);
    dir_ok = (st.type == T_DIR);
  }

  vfs_unlink("/selftest");
  vfs_rmdir("/selftest_dir");

  if (rw_ok && dir_ok) {
    print("[FS] selftest PASS\n", 0x0A);
  } else {
    print("[FS] selftest FAIL (rw=", 0x0C);
    print(rw_ok ? "ok" : "bad", 0x0C);
    print(", dir=", 0x0C);
    print(dir_ok ? "ok" : "bad", 0x0C);
    print(")\n", 0x0C);
  }
}
