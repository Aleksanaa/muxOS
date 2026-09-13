#include "syscall.h"
#include "console.h"
#include "process.h"
#include "../../drivers/input/keyboard.h"
#include "../../drivers/platform/reboot.h"
#include "../../drivers/video/vga.h"
#include "../fs/fs.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include <stdint.h>

static struct file **cur_fds(void) { return processes[current].fds; }

int syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx,
                    uint32_t esi, uint32_t edi, uint32_t ebp) {
  (void)esi;
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
    print("task exit.\n", 0);
    process_exit();
    break;

  case SYS_FORK:
    return process_fork(0);

  case SYS_SLEEP:
    if (ebx > 0)
      process_sleep(ebx);
    break;

  case SYS_EXEC: {
    uint32_t start = ebx; // 起始地址
    uint32_t size = ecx;  // 大小
    process_t *p = &processes[current];
    // 先分配新页
    uint32_t code_size = size;
    if (code_size > 4096)
      code_size = 4096;
    uint32_t code_page = vmm_alloc();
    if (!code_page)
      break;

    // 复制代码（在释放旧页之前）
    uint8_t *src = (uint8_t *)(uintptr_t)start;
    uint8_t *dst = (uint8_t *)(uintptr_t)code_page;
    for (uint32_t i = 0; i < code_size; i++)
      dst[i] = src[i];

    // 分配新栈
    uint32_t user_stack_base = vmm_alloc();
    for (int i = 1; i < 4; i++)
      vmm_alloc();
    uint32_t user_stack = user_stack_base + 4 * 4096;

    // 现在释放旧页
    vmm_free(p->user_code);
    for (int i = 0; i < 4; i++)
      vmm_free(p->user_stack - 4096 * (i + 1));

    // patch iret frame so syscall_stub returns into the new program
    extern uint32_t syscall_kernel_esp;
    uint32_t *iret = (uint32_t *)(uintptr_t)syscall_kernel_esp;
    iret[0] = code_page;  // EIP
    iret[3] = user_stack; // ESP_user
    processes[current].ctx.esp = code_page;
    processes[current].ctx.ebp = user_stack;
    processes[current].ctx.ebx = 0;
    processes[current].ctx.esi = 0;
    processes[current].ctx.edi = 0;
    processes[current].user_code = code_page;
    processes[current].user_stack = user_stack;
    processes[current].state = PROC_RUNNING;
    processes[current].parent_pid = 0;
    break;
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
    struct file *f = vfs_open((const char *)ebx, (int)ecx);
    if (!f)
      return -1;
    int fd = fdalloc(cur_fds(), f);
    if (fd < 0) {
      fileclose(f);
      return -1;
    }
    return fd;
  }

  case SYS_CLOSE:
    fdclose(cur_fds(), (int)ebx);
    return 0;

  case SYS_CREAT: {
    struct file *f = vfs_open((const char *)ebx, O_CREAT | O_WRONLY | O_TRUNC);
    if (!f)
      return -1;
    int fd = fdalloc(cur_fds(), f);
    if (fd < 0) {
      fileclose(f);
      return -1;
    }
    return fd;
  }

  case SYS_UNLINK:
    return vfs_unlink((const char *)ebx);

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
    struct inode *ip = namei((const char *)ebx);
    if (!ip)
      return -1;
    stati(ip, (struct stat *)ecx);
    return 0;
  }

  case SYS_FSTAT:
    return filestat(fdget(cur_fds(), (int)ebx), (struct stat *)ecx);

  case SYS_MKDIR:
    return vfs_mkdir((const char *)ebx);

  case SYS_GETDENTS: {
    struct file *f = fdget(cur_fds(), (int)ebx);
    if (!f || !f->ip || f->ip->type != T_DIR)
      return -1;
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
      kmemcpy(ubuf + written, &de, sizeof(de));
      written += sizeof(de);
    }
    return written;
  }

  case SYS_DUP: {
    struct file *f = fdget(cur_fds(), (int)ebx);
    if (!f)
      return -1;
    return fdalloc(cur_fds(), filedup(f));
  }

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
