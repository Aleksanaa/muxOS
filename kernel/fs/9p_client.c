/*
 * 9p_client.c - 9P2000.u client operations and the VFS bridge.
 *
 * Sits on top of the virtio-9p transport (drivers/virtio/virtio_9p.c).  A
 * host directory attached with QEMU's -virtfs is presented as a normal
 * struct inode subtree: directory listings are materialised into the memfs
 * dirent format so the existing getdents/dirlookup paths work unchanged, and
 * file contents are read/written on demand with Tread/Twrite.
 *
 * Each inode keeps an *unopened* fid so it can be cloned (Twalk with no
 * names) for directory reads; each open file description clones it again and
 * opens the clone with its own access mode, so opens do not interfere.
 */

#include "9p.h"
#include "fs.h"
#include "pmm.h"
#include "string.h"
#include "vga.h"

/* Fids are handed out monotonically and not recycled; the inode table caps
 * how many can be outstanding, and QEMU's fid pool is large. */
static uint32_t next_fid = 1;

/* Scratch buffer for a directory read: walking each entry issues further RPCs
 * that would otherwise overwrite the response buffer we are parsing. */
static uchar *dirbuf;
static uint32_t dirbuf_size;

static uint32_t alloc_fid(void) { return next_fid++; }

/*
 * Twalk `fid`.  With a name, walk one component into a fresh fid and return
 * its qid; with name == 0, clone the fid (used to read a directory without
 * opening the fid we still need for walking).
 */
static int walk(uint32_t fid, const char *name, uint32_t *out, Qid *qid) {
  Fcall tx, rx;
  uint32_t nf = alloc_fid();

  kmemset(&tx, 0, sizeof(tx));
  tx.type = Twalk;
  tx.fid = fid;
  tx.newfid = nf;
  if (name) {
    tx.nwname = 1;
    tx.wname[0] = (char *)name;
  }
  if (v9p_rpc(&tx, &rx) < 0)
    return -1;
  if (rx.nwqid != tx.nwname)
    return -1;
  if (qid && tx.nwname)
    *qid = rx.wqid[0];
  *out = nf;
  return 0;
}

static int open_fid(uint32_t fid, int mode) {
  Fcall tx, rx;
  kmemset(&tx, 0, sizeof(tx));
  tx.type = Topen;
  tx.fid = fid;
  tx.mode = (uchar)mode;
  return v9p_rpc(&tx, &rx);
}

/* Issue a Tread; on success rx->data/rx->count point into the shared
 * response buffer and stay valid only until the next RPC. */
static int read_raw(uint32_t fid, uint64_t off, uint32_t count, Fcall *rx) {
  Fcall tx;
  kmemset(&tx, 0, sizeof(tx));
  tx.type = Tread;
  tx.fid = fid;
  tx.offset = (vlong)off;
  tx.count = count;
  if (v9p_rpc(&tx, rx) < 0)
    return -1;
  return (int)rx->count;
}

static int clunk(uint32_t fid) {
  Fcall tx, rx;
  kmemset(&tx, 0, sizeof(tx));
  tx.type = Tclunk;
  tx.fid = fid;
  return v9p_rpc(&tx, &rx);
}

/* 9P open-mode bits. */
#define P9_OREAD 0
#define P9_OWRITE 1
#define P9_ORDWR 2
#define P9_OTRUNC 0x10
#define P9_DMDIR 0x80000000u

/*
 * Clone the inode's (unopened) fid and open the clone with the mode implied
 * by the VFS flags.  Each open file description gets its own fid, so a
 * write-only open cannot break a later read of the same inode.
 */
int v9p_open_file(struct inode *ip, int flags, uint32_t *out_fid) {
  int acc = flags & O_ACCMODE;
  int mode = (acc == O_WRONLY) ? P9_OWRITE
           : (acc == O_RDWR) ? P9_ORDWR
                             : P9_OREAD;
  uint32_t nf;

  if (ip->fid == NOFID)
    return -1;
  if (walk(ip->fid, 0, &nf, 0) < 0)
    return -1;
  if (flags & O_TRUNC)
    mode |= P9_OTRUNC;

  if (open_fid(nf, mode) < 0) {
    /* Read-only host file opened for writing: fall back to read-only. */
    if (mode == P9_OREAD || open_fid(nf, P9_OREAD) < 0) {
      clunk(nf);
      return -1;
    }
  }
  *out_fid = nf;
  return 0;
}

int v9p_file_read(uint32_t fid, void *dst, uint32_t off, uint32_t n) {
  Fcall rx;
  uint32_t max = v9p_get_msize() - IOHDRSZ;
  int r;

  if (n > max)
    n = max;
  r = read_raw(fid, off, n, &rx);
  if (r < 0)
    return -1;
  if ((uint32_t)r > n)
    r = (int)n;
  if (r > 0)
    kmemcpy(dst, rx.data, (uint32_t)r);
  return r;
}

int v9p_file_write(uint32_t fid, const void *src, uint32_t off, uint32_t n) {
  Fcall tx, rx;
  uint32_t max = v9p_get_msize() - IOHDRSZ;

  if (n > max)
    n = max;
  kmemset(&tx, 0, sizeof(tx));
  tx.type = Twrite;
  tx.fid = fid;
  tx.offset = (vlong)off;
  tx.count = n;
  tx.data = (char *)src;
  if (v9p_rpc(&tx, &rx) < 0)
    return -1;
  return (int)rx.count;
}

int v9p_close(uint32_t fid) { return fid ? clunk(fid) : 0; }

int v9p_truncate(uint32_t fid, uint32_t size) {
  Dir d;
  Fcall tx, rx;
  uchar stat[STATFIXLEN]; /* an all-empty Dir encodes to exactly this */
  uint n;

  /* Twstat: only `length` is meaningful; every other field is the
   * "do not change" sentinel (all ones / empty string). */
  kmemset(&d, 0, sizeof(d));
  d.type = 0xFFFF;
  d.dev = 0xFFFFFFFFu;
  d.qid.type = 0xFF;
  d.qid.vers = 0xFFFFFFFFu;
  d.qid.path = ~(uvlong)0;
  d.mode = 0xFFFFFFFFu;
  d.atime = 0xFFFFFFFFu;
  d.mtime = 0xFFFFFFFFu;
  d.length = (vlong)size;

  n = convD2M(&d, stat, sizeof(stat));
  if (n == 0)
    return -1;

  kmemset(&tx, 0, sizeof(tx));
  tx.type = Twstat;
  tx.fid = fid;
  tx.nstat = (ushort)n;
  tx.stat = stat;
  if (v9p_rpc(&tx, &rx) < 0)
    return -1;
  return 0;
}

/*
 * Create a file (Tcreate on a *clone* of the parent fid, since Tcreate turns
 * its fid argument into the new file).  Returns the new inode, or 0.
 */
struct inode *v9p_create(struct inode *dp, const char *name, int flags) {
  Fcall tx, rx;
  uint32_t cfid;
  int acc = flags & O_ACCMODE;
  int omode = (acc == O_WRONLY) ? P9_OWRITE
            : (acc == O_RDWR) ? P9_ORDWR
                              : P9_OREAD;
  struct inode *ip;

  if (dp->fid == NOFID || walk(dp->fid, 0, &cfid, 0) < 0) {
    fs_errno = EIO;
    return 0;
  }
  if (flags & O_TRUNC)
    omode |= P9_OTRUNC;

  kmemset(&tx, 0, sizeof(tx));
  tx.type = Tcreate;
  tx.fid = cfid;
  tx.name = (char *)name;
  tx.perm = 0644;
  tx.mode = (uchar)omode;
  tx.ext = "";
  if (v9p_rpc(&tx, &rx) < 0) {
    clunk(cfid);
    fs_errno = EACCES;
    return 0;
  }

  ip = ialloc(T_FILE, 0, 0);
  if (!ip) {
    clunk(cfid);
    fs_errno = ENOSPC;
    return 0;
  }
  /* Tcreate opened cfid as the new file; drop it and hold an unopened fid,
   * so each open() can clone it with its own mode. */
  clunk(cfid);
  ip->backend = INODE_9P;
  ip->parent = dp->inum;
  ip->fid = NOFID;
  {
    uint32_t nf;
    if (walk(dp->fid, name, &nf, 0) == 0)
      ip->fid = nf;
  }
  ip->size = 0;
  ip->mode = MODE_FILE;
  dirlink(dp, name, ip->inum);
  return ip;
}

int v9p_mkdir(struct inode *dp, const char *name, uint16_t mode) {
  Fcall tx, rx;
  uint32_t cfid, nf;
  struct inode *ip;

  if (dp->fid == NOFID || walk(dp->fid, 0, &cfid, 0) < 0)
    return -1;

  kmemset(&tx, 0, sizeof(tx));
  tx.type = Tcreate;
  tx.fid = cfid;
  tx.name = (char *)name;
  tx.perm = P9_DMDIR | (mode & 0777);
  tx.mode = P9_OREAD;
  tx.ext = "";
  if (v9p_rpc(&tx, &rx) < 0) {
    clunk(cfid);
    return -1;
  }
  /* Tcreate opened cfid as the new directory; drop it and take a fresh,
   * unopened fid so the directory can later be walked. */
  clunk(cfid);

  ip = ialloc(T_DIR, 0, 0);
  if (!ip)
    return -1;
  ip->backend = INODE_9P;
  ip->parent = dp->inum;
  ip->mode = MODE_DIR;
  ip->size = 0;
  ip->fid = NOFID;
  if (walk(dp->fid, name, &nf, 0) == 0)
    ip->fid = nf;
  dirlink(dp, name, ip->inum);
  return 0;
}

int v9p_remove(struct inode *dp, const char *name) {
  Fcall tx, rx;
  uint32_t fid;
  uint32_t inum;
  struct inode *ip;

  if (dp->fid == NOFID || walk(dp->fid, name, &fid, 0) < 0)
    return -1;

  kmemset(&tx, 0, sizeof(tx));
  tx.type = Tremove;
  tx.fid = fid;
  if (v9p_rpc(&tx, &rx) < 0)
    return -1; /* Tremove clunks the fid server-side even on failure */

  inum = (uint32_t)dirlookup(dp, name, 0);
  dirunlink(dp, name);
  ip = inum ? iget(inum) : 0;
  if (ip)
    ifree(ip);
  return 0;
}

int v9p_loaddir(struct inode *ip) {
  Fcall rx;
  uint32_t rfid;
  uint32_t req = v9p_get_msize() - IOHDRSZ;
  uint64_t off = 0;
  int guard;

  /* Set before dirlink() so nested dirlookup() calls do not reload. */
  ip->dir_loaded = 1;
  if (ip->fid == NOFID)
    return -1;
  if (!dirbuf) {
    dirbuf_size = v9p_get_msize();
    dirbuf = (uchar *)(uintptr_t)pmm_alloc_contig(dirbuf_size / PAGE_SIZE);
  }
  if (!dirbuf)
    return -1;

  if (walk(ip->fid, 0, &rfid, 0) < 0)
    return -1;
  if (open_fid(rfid, 0) < 0) {
    clunk(rfid);
    return -1;
  }

  for (guard = 0; guard < 4096; guard++) {
    int r = read_raw(rfid, off, req, &rx);
    uchar *p;
    uchar *end;
    int processed = 0;

    if (r <= 0)
      break;
    if ((uint32_t)r > dirbuf_size)
      r = (int)dirbuf_size;
    kmemcpy(dirbuf, rx.data, (uint32_t)r);
    p = dirbuf;
    end = p + r;

    /*
     * 9P2000.u returns directory entries as stat structures, each preceded
     * by its 2-byte size (unlike 9P2000.L's qid/offset/type/name dirents).
     */
    while (p + 2 <= end) {
      uint16_t ssz = (uint16_t)(p[0] | (p[1] << 8));
      uint32_t total = (uint32_t)ssz + 2;
      Dir d;
      char strs[256];
      struct inode *cip;
      uint32_t cfid;

      if (p + total > end)
        break;
      if (convM2D(p, total, &d, strs) == 0)
        break;
      p += total;
      processed = 1;

      if (kstrcmp(d.name, ".") == 0 || kstrcmp(d.name, "..") == 0)
        continue;

      cip = ialloc((d.qid.type & 0x80) ? T_DIR : T_FILE, 0, 0);
      if (!cip)
        continue;
      cip->backend = INODE_9P;
      cip->parent = ip->inum;
      cip->fid = NOFID;
      /* A directory's `size` is the byte length of the memfs listing built
       * here, so it must start at 0; only files carry the remote length. */
      if (d.qid.type & 0x80) {
        cip->mode = MODE_DIR;
      } else {
        cip->size = (uint32_t)d.length;
        cip->mode = MODE_FILE;
      }

      if (walk(ip->fid, d.name, &cfid, 0) == 0)
        cip->fid = cfid;

      dirlink(ip, d.name, cip->inum);
    }

    if (!processed)
      break;
    /* A nonzero offset continues the same directory instead of rewinding. */
    off = 1;
  }

  clunk(rfid);
  return 0;
}

int v9p_mount(const char *path) {
  Fcall tx, rx;
  struct inode *ip;

  kmemset(&tx, 0, sizeof(tx));
  tx.type = Tversion;
  tx.tag = NOTAG;
  tx.msize = v9p_get_msize();
  tx.version = VERSION9P;
  if (v9p_rpc(&tx, &rx) < 0) {
    print("[9P] mount: version failed: ", 0x0C);
    print(v9p_errstr(), 0x0C);
    print("\n", 0x0C);
    return -1;
  }
  /* Adopt the server's (possibly smaller) message size. */
  v9p_set_msize(rx.msize);

  kmemset(&tx, 0, sizeof(tx));
  tx.type = Tattach;
  tx.tag = 0;
  tx.fid = 0;
  tx.afid = NOFID;
  tx.uname = "root";
  tx.aname = (char *)v9p_get_tag();
  tx.n_uname = 0;
  if (v9p_rpc(&tx, &rx) < 0) {
    print("[9P] mount: attach failed: ", 0x0C);
    print(v9p_errstr(), 0x0C);
    print("\n", 0x0C);
    return -1;
  }

  ip = namei(path);
  if (!ip || ip->type != T_DIR) {
    print("[9P] mount point missing\n", 0x0C);
    return -1;
  }

  itrunc(ip);
  ip->backend = INODE_9P;
  ip->fid = 0;
  ip->parent = ROOTINO;
  ip->dir_loaded = 0;
  ip->mode = MODE_DIR;
  ip->size = 0;

  print("[OK] mounted host dir at ", 0x0A);
  print(path, 0x0A);
  print("\n", 0x0A);
  return 0;
}
