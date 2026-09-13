/* muxOS stub for <mntent.h> (Linux mount-table API; unused on muxOS). */
#ifndef MUXOS_MNTENT_H
#define MUXOS_MNTENT_H

#include <stdio.h>

struct mntent {
  char *mnt_fsname;
  char *mnt_dir;
  char *mnt_type;
  char *mnt_opts;
  int mnt_freq;
  int mnt_passno;
};

/* The functions themselves are stubbed out via macros in compat.h. */

#endif
