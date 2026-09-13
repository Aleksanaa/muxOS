/* muxOS stub for toybox's <sys/mount.h> usage.
 * struct statfs comes from the force-included compat.h; statfs() is unused
 * on muxOS and dropped by --gc-sections. */
#ifndef MUXOS_SYS_MOUNT_H
#define MUXOS_SYS_MOUNT_H

int statfs(const char *path, struct statfs *buf);

#endif
