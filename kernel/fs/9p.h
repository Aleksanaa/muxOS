#ifndef NINE_P_H
#define NINE_P_H

/*
 * 9P2000 protocol definitions.
 *
 * Adapted from Plan 9 / 9front <fcall.h> (MIT / Lucent Public License):
 *   sys/include/fcall.h, sys/src/libc/9sys/convS2M.c, convM2S.c,
 *   convD2M.c, convM2D.c.
 *
 * The original code uses Plan 9's <u.h>/<libc.h> types; they are mapped here
 * to fixed-width C types so the marshalling compiles in the freestanding
 * kernel.  See 9p_conv.c.
 */

#include <stdint.h>

typedef uint8_t uchar;
typedef uint16_t ushort;
typedef uint32_t uint;
typedef uint32_t u32int;
typedef uint64_t uvlong;
typedef int64_t vlong;

/*
 * QEMU's 9p server (as of 11.x) only recognises "9P2000.u" and "9P2000.L",
 * not the plain "9P2000" string.  ".u" keeps the classic message set (with a
 * few extension fields), which is what the vendored conv code speaks.
 */
#define VERSION9P "9P2000.u"
#define MAXWELEM 16

typedef struct Qid {
  uchar type;
  uint vers;
  uvlong path;
} Qid;

/* Directory entry as returned by Tstat / Rstat. */
typedef struct Dir {
  ushort type;
  uint dev;
  Qid qid;
  uint mode;
  uint atime;
  uint mtime;
  vlong length;
  char *name;
  char *uid;
  char *gid;
  char *muid;
} Dir;

typedef struct Fcall {
  uchar type;
  u32int fid;
  ushort tag;
  union {
    struct {
      u32int msize;   /* Tversion, Rversion */
      char *version;  /* Tversion, Rversion */
    };
    struct {
      ushort oldtag; /* Tflush */
    };
    struct {
      char *ename; /* Rerror */
    };
    struct {
      Qid qid;      /* Rattach, Ropen, Rcreate */
      u32int iounit; /* Ropen, Rcreate */
    };
    struct {
      Qid aqid; /* Rauth */
    };
    struct {
      u32int afid;    /* Tauth, Tattach */
      char *uname;    /* Tauth, Tattach */
      char *aname;    /* Tauth, Tattach */
      u32int n_uname; /* 9P2000.u Tattach: numeric uname */
    };
    struct {
      u32int perm;  /* Tcreate */
      char *name;   /* Tcreate */
      uchar mode;   /* Tcreate, Topen */
      char *ext;    /* 9P2000.u Tcreate: extension string */
    };
    struct {
      u32int newfid;         /* Twalk */
      ushort nwname;         /* Twalk */
      char *wname[MAXWELEM]; /* Twalk */
    };
    struct {
      ushort nwqid;         /* Rwalk */
      Qid wqid[MAXWELEM];   /* Rwalk */
    };
    struct {
      vlong offset; /* Tread, Twrite */
      u32int count; /* Tread, Twrite, Rread */
      char *data;   /* Twrite, Rread */
    };
    struct {
      ushort nstat; /* Twstat, Rstat */
      uchar *stat;  /* Twstat, Rstat */
    };
  };
} Fcall;

#define GBIT8(p) (((uchar *)(p))[0])
#define GBIT16(p) (((uchar *)(p))[0] | (((uchar *)(p))[1] << 8))
#define GBIT32(p)                                                              \
  (((uchar *)(p))[0] | (((uchar *)(p))[1] << 8) | (((uchar *)(p))[2] << 16) | \
   (((uchar *)(p))[3] << 24))
#define GBIT64(p)                                                              \
  ((u32int)(((uchar *)(p))[0] | (((uchar *)(p))[1] << 8) |                     \
            (((uchar *)(p))[2] << 16) | (((uchar *)(p))[3] << 24)) |           \
   ((uvlong)(((uchar *)(p))[4] | (((uchar *)(p))[5] << 8) |                    \
             (((uchar *)(p))[6] << 16) | (((uchar *)(p))[7] << 24))            \
    << 32))

#define PBIT8(p, v)                                                            \
  do {                                                                         \
    (p)[0] = (uchar)(v);                                                       \
  } while (0)
#define PBIT16(p, v)                                                           \
  do {                                                                         \
    (p)[0] = (uchar)(v);                                                       \
    (p)[1] = (uchar)((v) >> 8);                                                \
  } while (0)
#define PBIT32(p, v)                                                           \
  do {                                                                         \
    (p)[0] = (uchar)(v);                                                       \
    (p)[1] = (uchar)((v) >> 8);                                                \
    (p)[2] = (uchar)((v) >> 16);                                               \
    (p)[3] = (uchar)((v) >> 24);                                               \
  } while (0)
#define PBIT64(p, v)                                                           \
  do {                                                                         \
    (p)[0] = (uchar)(v);                                                       \
    (p)[1] = (uchar)((v) >> 8);                                                \
    (p)[2] = (uchar)((v) >> 16);                                               \
    (p)[3] = (uchar)((v) >> 24);                                               \
    (p)[4] = (uchar)((v) >> 32);                                               \
    (p)[5] = (uchar)((v) >> 40);                                               \
    (p)[6] = (uchar)((v) >> 48);                                               \
    (p)[7] = (uchar)((v) >> 56);                                               \
  } while (0)

#define BIT8SZ 1
#define BIT16SZ 2
#define BIT32SZ 4
#define BIT64SZ 8
#define QIDSZ (BIT8SZ + BIT32SZ + BIT64SZ)

/* STATFIXLEN includes the leading 16-bit count (which excludes itself). */
#define STATFIXLEN (BIT16SZ + QIDSZ + 5 * BIT16SZ + 4 * BIT32SZ + 1 * BIT64SZ)

#define NOTAG ((ushort)~0U)
#define NOFID ((u32int)~0U)
#define IOHDRSZ 24

enum {
  Tversion = 100,
  Rversion,
  Tauth = 102,
  Rauth,
  Tattach = 104,
  Rattach,
  Terror = 106,
  Rerror,
  Tflush = 108,
  Rflush,
  Twalk = 110,
  Rwalk,
  Topen = 112,
  Ropen,
  Tcreate = 114,
  Rcreate,
  Tread = 116,
  Rread,
  Twrite = 118,
  Rwrite,
  Tclunk = 120,
  Rclunk,
  Tremove = 122,
  Rremove,
  Tstat = 124,
  Rstat,
  Twstat = 126,
  Rwstat,
  Tmax,
};

/* Message marshalling (9p_conv.c). */
uint sizeS2M(Fcall *f);
uint convS2M(Fcall *f, uchar *ap, uint nap);
uint convM2S(uchar *ap, uint nap, Fcall *f);
uint convD2M(Dir *d, uchar *buf, uint nbuf);
uint convM2D(uchar *buf, uint nbuf, Dir *d, char *strs);

/* virtio-9p transport + client (drivers/virtio/virtio_9p.c). */
int v9p_init(void);
int v9p_rpc(Fcall *tx, Fcall *rx);
const char *v9p_errstr(void);
const char *v9p_get_tag(void);
uint32_t v9p_get_msize(void);
void v9p_set_msize(uint32_t m);

#endif
