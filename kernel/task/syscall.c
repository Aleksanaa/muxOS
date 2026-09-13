#include "syscall.h"
#include "console.h"
#include "gdt.h"
#include "process.h"
#include "../../drivers/input/keyboard.h"
#include "../../drivers/platform/reboot.h"
#include "../../drivers/video/vga.h"
#include "../fs/fs.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include <stdint.h>

static struct file **cur_fds(void) { return processes[current].fds; }

#define AT_FDCWD_K (-100)
#define AT_REMOVEDIR_K 0x200

/*
 * Inode that a relative path in an *at() syscall starts from.  AT_FDCWD means
 * the cwd (returned as NULL, which the VFS treats as "cwd"); a directory fd
 * returns its inode.  *err is set on failure.
 */
static struct inode *path_base(int dirfd, int *err) {
  struct file *f;

  if (dirfd == AT_FDCWD_K) {
    *err = 0;
    return 0;
  }
  f = fdget(cur_fds(), dirfd);
  if (!f || !f->ip) {
    *err = EBADF;
    return 0;
  }
  if (f->ip->type != T_DIR) {
    *err = ENOTDIR;
    return 0;
  }
  *err = 0;
  return f->ip;
}

/* Anonymous mmap region: a simple bump allocator over the user address space. */
#define USER_MMAP_BASE 0x30000000u
static uint32_t mmap_next = USER_MMAP_BASE;

void mmap_reset(void) {
  for (uint32_t va = USER_MMAP_BASE; va < mmap_next; va += 0x1000)
    vmm_free(processes[current].pdir, va);
  mmap_next = USER_MMAP_BASE;
}

int syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx,
                    uint32_t esi, uint32_t edi, uint32_t ebp) {
  (void)edi;
  (void)ebp;
  switch (eax) {
  case SYS_READ: {
    struct file *f = fdget(cur_fds(), (int)ebx);
    if (!f)
      return -1;
    return fileread(f, (void *)ecx, (int)edx);
  }

  case SYS_WRITE: {
    struct file *f = fdget(cur_fds(), (int)ebx);
    if (!f)
      return -1;
    return filewrite(f, (const void *)ecx, (int)edx);
  }

  case SYS_EXIT:
    if (processes[current].pid == (uint32_t)shell_pid &&
        processes[current].exec_active) {
      processes[current].exec_active = 0;
      if (process_restore_shell() == 0)
        break; // iret back into the shell
    }
    process_exit();
    break;

  case SYS_FORK:
    return process_fork(0);

  case SYS_SLEEP:
    if (ebx > 0)
      process_sleep(ebx);
    break;

  case SYS_EXECVE: {
    int r = process_execve((const char *)ebx, (const char *const *)ecx);
    if (r < 0)
      return r;
    processes[current].exec_active = 1;
    return 0;
  }

  case SYS_WAIT:
    return process_wait();

  case SYS_RESTART_SYSCALL:
    machine_restart();
    break;

  case SYS_SHUTDOWN:
    machine_shutdown();
    break;

  case SYS_POWEROFF:
    machine_power_off();
    break;

  case SYS_OPEN: {
    fs_errno = 0;
    struct file *f = vfs_open((const char *)ebx, (int)ecx);
    if (!f)
      return -fs_error();
    int fd = fdalloc(cur_fds(), f);
    if (fd < 0) {
      fileclose(f);
      return -EMFILE;
    }
    return fd;
  }

  case SYS_CLOSE:
    fdclose(cur_fds(), (int)ebx);
    return 0;

  case SYS_CREAT: {
    fs_errno = 0;
    struct file *f = vfs_open((const char *)ebx, O_CREAT | O_WRONLY | O_TRUNC);
    if (!f)
      return -fs_error();
    int fd = fdalloc(cur_fds(), f);
    if (fd < 0) {
      fileclose(f);
      return -EMFILE;
    }
    return fd;
  }

  case SYS_UNLINK:
    fs_errno = 0;
    if (vfs_unlink((const char *)ebx) < 0)
      return -fs_error();
    return 0;

  case SYS_RMDIR:
    fs_errno = 0;
    if (vfs_rmdir((const char *)ebx) < 0)
      return -fs_error();
    return 0;

  case SYS_RENAME:
    fs_errno = 0;
    if (vfs_rename((const char *)ebx, (const char *)ecx) < 0)
      return -fs_error();
    return 0;

  case SYS_LINK:
    fs_errno = 0;
    if (vfs_link((const char *)ebx, (const char *)ecx) < 0)
      return -fs_error();
    return 0;

  case SYS_CHMOD:
    fs_errno = 0;
    if (vfs_chmod((const char *)ebx, (uint16_t)ecx) < 0)
      return -fs_error();
    return 0;

  case SYS_FCHMOD: {
    struct file *f = fdget(cur_fds(), (int)ebx);
    if (!f || !f->ip)
      return -EBADF;
    f->ip->mode = (uint16_t)ecx & 07777;
    return 0;
  }

  case SYS_CHDIR:
    fs_errno = 0;
    if (vfs_chdir((const char *)ebx) < 0)
      return -fs_error();
    return 0;

  case SYS_GETCWD:
    fs_errno = 0;
    if (vfs_getcwd((char *)ebx, ecx) < 0)
      return -fs_error();
    return 0;

  case SYS_OPENAT: {
    int err;
    struct inode *base = path_base((int)ebx, &err);
    if (err)
      return -err;
    fs_errno = 0;
    struct file *f = vfs_open_at(base, (const char *)ecx, (int)edx);
    if (!f)
      return -fs_error();
    int fd = fdalloc(cur_fds(), f);
    if (fd < 0) {
      fileclose(f);
      return -EMFILE;
    }
    return fd;
  }

  case SYS_STATAT: {
    int err;
    struct inode *base = path_base((int)ebx, &err);
    if (err)
      return -err;
    fs_errno = 0;
    if (vfs_stat_at(base, (const char *)ecx, (struct stat *)edx) < 0)
      return -fs_error();
    return 0;
  }

  case SYS_UNLINKAT: {
    int err;
    struct inode *base = path_base((int)ebx, &err);
    if (err)
      return -err;
    fs_errno = 0;
    int r = ((int)edx & AT_REMOVEDIR_K)
                ? vfs_rmdir_at(base, (const char *)ecx)
                : vfs_unlink_at(base, (const char *)ecx);
    if (r < 0)
      return -fs_error();
    return 0;
  }

  case SYS_MKDIRAT: {
    int err;
    struct inode *base = path_base((int)ebx, &err);
    if (err)
      return -err;
    fs_errno = 0;
    if (vfs_mkdir_at(base, (const char *)ecx) < 0)
      return -fs_error();
    return 0;
  }

  case SYS_RENAMEAT: {
    int e1, e2;
    struct inode *obase = path_base((int)ebx, &e1);
    if (e1)
      return -e1;
    struct inode *nbase = path_base((int)edx, &e2);
    if (e2)
      return -e2;
    fs_errno = 0;
    if (vfs_rename_at(obase, (const char *)ecx, nbase, (const char *)esi) < 0)
      return -fs_error();
    return 0;
  }

  case SYS_LINKAT: {
    int e1, e2;
    struct inode *obase = path_base((int)ebx, &e1);
    if (e1)
      return -e1;
    struct inode *nbase = path_base((int)edx, &e2);
    if (e2)
      return -e2;
    fs_errno = 0;
    if (vfs_link_at(obase, (const char *)ecx, nbase, (const char *)esi) < 0)
      return -fs_error();
    return 0;
  }

  case SYS_LSEEK: {
    struct file *f = fdget(cur_fds(), (int)ebx);
    if (!f || f->type != FD_INODE)
      return -1;
    int base = 0;
    if ((int)edx == SEEK_CUR)
      base = (int)f->off;
    else if ((int)edx == SEEK_END)
      base = (int)f->ip->size;
    int newoff = base + (int)ecx;
    if (newoff < 0)
      return -1;
    f->off = (uint32_t)newoff;
    return newoff;
  }

  case SYS_STAT: {
    fs_errno = 0;
    struct inode *ip = namei((const char *)ebx);
    if (!ip)
      return -fs_error();
    stati(ip, (struct stat *)ecx);
    return 0;
  }

  case SYS_FSTAT: {
    struct file *f = fdget(cur_fds(), (int)ebx);
    if (!f)
      return -EBADF;
    return filestat(f, (struct stat *)ecx);
  }

  case SYS_MKDIR:
    fs_errno = 0;
    if (vfs_mkdir((const char *)ebx) < 0)
      return -fs_error();
    return 0;

  case SYS_GETDENTS: {
    struct file *f = fdget(cur_fds(), (int)ebx);
    if (!f)
      return -EBADF;
    if (!f->ip || f->ip->type != T_DIR)
      return -ENOTDIR;
    char *ubuf = (char *)ecx;
    int max = (int)edx;
    int written = 0;
    struct dirent de;
    while (f->off + sizeof(de) <= f->ip->size &&
           written + (int)sizeof(de) <= max) {
      if (readi(f->ip, &de, f->off, sizeof(de)) != (int)sizeof(de))
        break;
      f->off += sizeof(de);
      if (de.inum == 0)
        continue;
      de.off = f->off;
      kmemcpy(ubuf + written, &de, sizeof(de));
      written += (int)sizeof(de);
    }
    return written;
  }

  case SYS_FTRUNCATE: {
    struct file *f = fdget(cur_fds(), (int)ebx);
    if (!f)
      return -EBADF;
    fs_errno = 0;
    if (filetruncate(f, ecx) < 0)
      return -fs_error();
    return 0;
  }

  case SYS_DUP2: {
    int oldfd = (int)ebx, newfd = (int)ecx;
    struct file *f = fdget(cur_fds(), oldfd);
    if (!f)
      return -EBADF;
    if (oldfd == newfd)
      return newfd;
    if (newfd < 0 || newfd >= FD_MAX)
      return -EBADF;
    fdclose(cur_fds(), newfd);
    cur_fds()[newfd] = filedup(f);
    return newfd;
  }

  case SYS_DUP: {
    struct file *f = fdget(cur_fds(), (int)ebx);
    if (!f)
      return -1;
    return fdalloc(cur_fds(), filedup(f));
  }

  case SYS_MMAP: {
    /* mmap(addr, len, prot, flags, fd, off) - only anonymous is supported. */
    uint32_t len = ecx;
    if (len == 0)
      return -1;
    uint32_t pages = (len + 0xFFFu) / 0x1000u;
    uint32_t base = mmap_next;
    for (uint32_t i = 0; i < pages; i++) {
      uint32_t va = base + i * 0x1000u;
      if (!vmm_alloc_at(processes[current].pdir, va))
        return -1;
      kmemset((void *)(uintptr_t)va, 0, 0x1000);
    }
    mmap_next = base + pages * 0x1000u;
    return (int)base;
  }

  case SYS_MUNMAP:
    /* The bump allocator never reuses memory; treat unmap as a no-op. */
    return 0;

  case SYS_SET_TLS:
    gdt_set_tls_base(ebx);
    return 0;

  case SYS_SETUID:
  case SYS_GETUID:
    return 0;

  case SYS_CLEAR:
    clear_screen();
    break;

  case SYS_GETCHAR:
    asm volatile("sti");
    return console_getchar();

  case SYS_VGASPACE:
    vga_backspace();
    break;

  case SYS_GETPID:
    return process_current_pid();

  case SYS_GET_PROCESS_COUNT:
    return process_get_count();

  case SYS_GET_PROCESS_INFO:
    return process_get_info(ebx, (process_info_t *)ecx);

  default:
    break;
  }
  return 0;
}
