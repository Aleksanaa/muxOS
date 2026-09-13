#include "process.h"
#include "../lib/string.h"
#include "elf.h"
#include "fs.h"
#include "pmm.h"
#include "tss.h"
#include "vga.h"
#include "vmm.h"
#include <stdint.h>
process_t processes[MAX_PROCESSES];
int current = 0;
int process_count = 0;
int shell_pid = -1;

extern void enter_usermode(uint32_t entry, uint32_t stack);

/* The userland ELF is embedded in the kernel image by objcopy. */
extern const uint8_t _binary_build_user_embedded_elf_start[];
extern const uint8_t _binary_build_user_embedded_elf_end[];

#define USER_STACK_TOP 0x28000000u
#define USER_STACK_PAGES 16u

void context_switch(context_t *old, context_t *new);
void process_enter(context_t *old, context_t *new);
void process_jump(context_t *new);

/*
 * Lay out argc/argv/envp and a minimal auxv at the top of the user stack,
 * newest at the lowest address.  The stack pages must already be mapped.
 */
static uint32_t user_build_stack(const char *const *args, int argc) {
  uint32_t sp = USER_STACK_TOP;
  uint32_t argp[32];

  if (argc > 32)
    argc = 32;
  for (int i = 0; i < argc; i++) {
    uint32_t len = kstrlen(args[i]) + 1;
    sp -= len;
    kmemcpy((void *)(uintptr_t)sp, args[i], len);
    argp[i] = sp;
    sp &= ~3u;
  }

  sp &= ~15u; // 16-byte align, as the SysV i386 ABI expects at entry
  sp -= 8;    // padding
  *(uint32_t *)(uintptr_t)sp = 0;
  *(uint32_t *)(uintptr_t)(sp + 4) = 0;
  sp -= 8; // auxv: AT_NULL (0)
  *(uint32_t *)(uintptr_t)sp = 0;
  *(uint32_t *)(uintptr_t)(sp + 4) = 0;
  sp -= 4; // envp terminator
  *(uint32_t *)(uintptr_t)sp = 0;
  sp -= 4; // argv terminator
  *(uint32_t *)(uintptr_t)sp = 0;
  for (int i = argc - 1; i >= 0; i--) {
    sp -= 4;
    *(uint32_t *)(uintptr_t)sp = argp[i];
  }
  sp -= 4; // argc
  *(uint32_t *)(uintptr_t)sp = (uint32_t)argc;
  return sp;
}

/*
 * Point the pending syscall return at a new program.  The stub restores the
 * pusha frame and iret's, so both the iret frame (EIP/ESP) and the saved
 * registers live just above the kernel esp captured on syscall entry.
 */
static void patch_user_frame(uint32_t entry, uint32_t stack) {
  uint32_t *iret = (uint32_t *)(uintptr_t)syscall_kernel_esp;
  iret[0] = entry; // EIP
  iret[3] = stack; // ESP_user

  uint32_t *regs = iret - 8; // pusha frame: edi..eax
  for (int i = 0; i < 8; i++)
    regs[i] = 0;
}

void process_register_current() {
  processes[0].pid = 0;
  processes[0].started = 1;
  processes[0].kernel_stack = 0;
  processes[0].state = PROC_RUNNING;
  processes[0].pdir = vmm_kernel_pdir();
  kstrcpy(processes[0].process_name, "bootstrap");
  fd_init(processes[0].fds);
  process_count = 1;
}

void process_schedule() {
  if (process_count < 2)
    return;

  // decrement sleep counters for all processes
  for (int i = 0; i < process_count; i++) {
    if (processes[i].sleep_ticks > 0)
      processes[i].sleep_ticks--;
  }

  // if current process is sleeping, switch to another process
  int need_switch = 0;
  if (processes[current].sleep_ticks > 0)
    need_switch = 1;

  // find next runnable process (skip pid=0 kernel_main, skip sleeping)
  int next = (current + 1) % process_count;
  int checked = 0;

  // 找到可用进程
  while (checked < process_count) {
    if (processes[next].pid != 0 &&
        processes[next].sleep_ticks == 0) // pid = 0 为内核进程
      break;
    next = (next + 1) % process_count;
    checked++;
  }

  if (!need_switch && (next == current || checked == process_count))
    return;

  // if no runnable process found, stay on current
  if (checked == process_count)
    return;

  int old = current;
  current = next;

  vmm_switch_pdir(processes[next].pdir);

  if (!processes[next].started) {
    processes[next].started = 1;
    process_enter(&processes[old].ctx, &processes[current].ctx);
  } else {
    context_switch(&processes[old].ctx, &processes[current].ctx);
  }
}

void process_create_kernel(void (*entry)()) {
  uint32_t stack_top = pmm_alloc() + 4096;

  stack_top -= 4;
  *(uint32_t *)stack_top = 0x200; // EFLAGS
  stack_top -= 4;
  *(uint32_t *)stack_top = 0x08; // CS 内核代码段
  stack_top -= 4;
  *(uint32_t *)stack_top = (uint32_t)entry; // EIP

  stack_top -= 32;

  processes[process_count].pid = process_count;
  processes[process_count].ctx.esp = stack_top;
  processes[process_count].ctx.ebp = 0;
  processes[process_count].ctx.ebx = 0;
  processes[process_count].ctx.esi = 0;
  processes[process_count].ctx.edi = 0;
  processes[process_count].started = 0;
  processes[process_count].kernel_stack = 0;
  processes[process_count].state = PROC_RUNNING;
  processes[process_count].pdir = vmm_kernel_pdir();
  kstrcpy(processes[process_count].process_name, "kernel_init");
  fd_init(processes[process_count].fds);
  process_count++;
}

void process_create_user(void) {
  extern void print(const char *, unsigned char);
  uint32_t entry = 0;
  uint32_t elf_size = (uint32_t)(_binary_build_user_embedded_elf_end -
                                 _binary_build_user_embedded_elf_start);

  uint32_t pdir = vmm_create_pdir();
  if (!pdir) {
    print("pdir alloc failed\n", 0x0C);
    return;
  }
  if (elf_load(pdir, _binary_build_user_embedded_elf_start, elf_size, &entry) <
      0) {
    print("elf load failed\n", 0x0C);
    return;
  }

  uint32_t stack_base = USER_STACK_TOP - USER_STACK_PAGES * 4096u;
  for (uint32_t i = 0; i < USER_STACK_PAGES; i++) {
    if (!vmm_alloc_at(pdir, stack_base + i * 4096)) {
      print("user stack map failed\n", 0x0C);
      return;
    }
  }

  /* Build the stack through the new address space. */
  uint32_t old_pdir = vmm_current_pdir();
  vmm_switch_pdir(pdir);
  static const char *init_argv[] = { "muxsh", 0 };
  uint32_t user_stack = user_build_stack(init_argv, 1);
  vmm_switch_pdir(old_pdir);

  /* 内核栈必须在内核区（无 PAGE_USER），不能用 vmm_alloc */
  uint32_t kernel_stack = pmm_alloc();
  if (!kernel_stack)
    return;
  kernel_stack += 4096;

  shell_pid = process_count;
  processes[process_count].pid = process_count;
  processes[process_count].ctx.esp = entry;
  processes[process_count].ctx.ebp = user_stack;
  processes[process_count].ctx.ebx = 0;
  processes[process_count].ctx.esi = 0;
  processes[process_count].ctx.edi = 0;
  processes[process_count].started = 0;
  processes[process_count].kernel_stack = kernel_stack;
  processes[process_count].user_code = entry;
  processes[process_count].user_stack = user_stack;
  processes[process_count].state = PROC_RUNNING;
  processes[process_count].parent_pid = 0;
  processes[process_count].pdir = pdir;
  processes[process_count].exec_active = 0;
  fd_init(processes[process_count].fds);
  process_count++;
}

void start_user_process(int pid, char *process_name) {
  for (int i = 0; i < process_count; i++) {
    process_t *p = &processes[i];
    if (p->pid != (uint32_t)pid || p->kernel_stack == 0 || p->started ||
        p->state != PROC_RUNNING)
      continue;

    kstrcpy(p->process_name, process_name);

    uint32_t *sp = (uint32_t *)p->kernel_stack;

    // Build the ring-3 IRET frame.
    *--sp = 0x23;          // SS
    *--sp = p->user_stack; // ESP
    *--sp = 0x202;         // EFLAGS: IF enabled
    *--sp = 0x1B;          // CS
    *--sp = p->user_code;  // EIP

    // Registers restored by POPA.
    for (int r = 0; r < 8; r++)
      *--sp = 0;

    p->ctx.esp = (uint32_t)sp;

    // Publish the task only after its context is ready.
    asm volatile("" ::: "memory");
    p->started = 1;
    return;
  }
}

void process_exit() {
  process_t *p = &processes[current];

  for (int i = 0; i < FD_MAX; i++) {
    if (p->fds[i]) {
      fileclose(p->fds[i]);
      p->fds[i] = 0;
    }
  }

  if (p->parent_pid > 0) {
    // 有父进程：变成僵尸，唤醒父进程
    p->state = PROC_ZOMBIE;
    // 找父进程，清除其 sleep_ticks 让调度器能切换过去
    for (int i = 0; i < process_count; i++) {
      if (processes[i].pid == p->parent_pid) {
        extern void print(const char *, unsigned char);
        extern void print_hex(uint32_t);
        processes[i].sleep_ticks = 0;
        break;
      }
    }
    return;
  }

  /* 无父进程：释放地址空间并删除。先切回内核页目录，才能销毁当前页目录。 */
  uint32_t dying_pdir = p->pdir;
  vmm_switch_pdir(vmm_kernel_pdir());
  if (dying_pdir && dying_pdir != vmm_kernel_pdir())
    vmm_destroy_pdir(dying_pdir);
  if (p->kernel_stack != 0)
    pmm_free(p->kernel_stack - 4096);

  for (int i = current; i < process_count - 1; i++)
    processes[i] = processes[i + 1];
  process_count--;

  if (process_count == 0) {
    for (;;)
      asm volatile("hlt");
  }

  if (current >= process_count)
    current = 0;

  if (current == 0 && process_count > 1)
    current = 1;

  vmm_switch_pdir(processes[current].pdir);
  processes[current].started = 1;
  process_jump(&processes[current].ctx);
}

// process_tick — called from irq0_stub and syscall_stub (in assembly).
// Decrements sleep counters, finds the next runnable process.
// Returns the new index if a switch should happen, -1 otherwise.
// IMPORTANT: updates `current` before returning so the asm stub can use
// the return value directly as the new process index.
int process_tick() {
  if (process_count < 2)
    return -1;

  for (int i = 0; i < process_count; i++) {
    if (processes[i].sleep_ticks > 0)
      processes[i].sleep_ticks--;
  }

  int next = (current + 1) % process_count;
  int checked = 0;

  while (checked < process_count) {
    // Kernel tasks have an initial interrupt frame. New user tasks need
    // start_user_process() before ctx.esp can be restored by popa + iret.
    if (processes[next].pid != 0 &&
        (processes[next].kernel_stack == 0 || processes[next].started) &&
        processes[next].sleep_ticks == 0 &&
        processes[next].state == PROC_RUNNING)
      break;
    next = (next + 1) % process_count;
    checked++;
  }

  if (checked == process_count || next == current)
    return -1;

  current = next;
  return next;
}

void process_sleep(uint32_t ticks) { processes[current].sleep_ticks = ticks; }

// Reap a zombie child. Returns child pid, or -1 if no zombie child exists.
// If no zombie but has children, sleeps briefly so scheduler can run children.
int process_wait() {
  uint32_t my_pid = processes[current].pid;
  for (int i = 0; i < process_count; i++) {
    if (processes[i].parent_pid == my_pid &&
        processes[i].state == PROC_ZOMBIE) {
      int pid = processes[i].pid;
      uint32_t cpdir = processes[i].pdir;
      if (processes[i].kernel_stack)
        pmm_free(processes[i].kernel_stack - 4096);
      if (cpdir && cpdir != vmm_kernel_pdir())
        vmm_destroy_pdir(cpdir);
      // remove from array
      for (int j = i; j < process_count - 1; j++)
        processes[j] = processes[j + 1];
      process_count--;
      if (current > i)
        current--;
      return pid;
    }
  }
  // no zombie yet — sleep so scheduler can run children
  processes[current].sleep_ticks = 10;
  return -1;
}

// set by syscall_stub before calling syscall_handler
uint32_t syscall_kernel_esp = 0;

int process_fork(uint32_t child_eax_ret) {
  (void)child_eax_ret;
  process_t *parent = &processes[current];

  if (process_count >= MAX_PROCESSES)
    return -1;

  /* Private address space: kernel PDEs plus a deep copy of every user page. */
  uint32_t cpdir = vmm_create_pdir();
  if (!cpdir)
    return -1;
  vmm_copy_pdir(cpdir, parent->pdir);

  /* Copy the kernel stack so the child resumes from the same syscall. */
  uint32_t ckstack = pmm_alloc();
  if (!ckstack) {
    vmm_destroy_pdir(cpdir);
    return -1;
  }
  if (parent->kernel_stack)
    kmemcpy((void *)(uintptr_t)ckstack,
            (void *)(uintptr_t)(parent->kernel_stack - 4096), 4096);
  uint32_t cktop = ckstack + 4096;

  int child = process_count;
  process_t *c = &processes[child];
  kmemset(c, 0, sizeof(*c));
  c->pid = (uint32_t)child;
  c->parent_pid = parent->pid;
  c->pdir = cpdir;
  c->kernel_stack = cktop;
  c->user_code = parent->user_code;
  c->user_stack = parent->user_stack;
  c->state = PROC_RUNNING;
  /* ctx.esp already points at a complete pusha/iret frame, so the child is
   * immediately runnable (unlike process_create_user, which needs the stub's
   * first-run handling). */
  c->started = 1;
  kstrcpy(c->process_name, parent->process_name);
  fd_fork(parent->fds, c->fds);

  /*
   * The CPU left the ring-3 registers in a pusha frame just below the iret
   * frame at syscall entry.  Point the child at the copy of that frame and
   * zero its eax so fork() returns 0 in the child.
   */
  uint32_t child_pusha = cktop - (parent->kernel_stack - (syscall_kernel_esp - 32));
  c->ctx.esp = child_pusha;
  *(uint32_t *)(uintptr_t)(child_pusha + 28) = 0; // eax

  process_count++;
  return child;
}

/*
 * Replace the current process image with the ELF stored at `path`.  argv is
 * snapshotted first because loading the new image overwrites user memory.
 * On success the pending syscall return points at the new entry point and
 * this returns 0; on failure it returns a negative errno.
 */
int process_execve(const char *path, const char *const *uargv) {
  struct file *f = vfs_open(path, O_RDONLY);
  if (!f)
    return -fs_errno;
  if (f->ip->type != T_FILE) {
    fileclose(f);
    return -EACCES;
  }

  char argbuf[512];
  const char *kargv[32];
  int argc = 0;
  uint32_t used = 0;
  if (uargv) {
    while (argc < 32 && uargv[argc] && used < sizeof(argbuf) - 1) {
      const char *s = uargv[argc];
      uint32_t j = 0;
      while (s[j] && used + j < sizeof(argbuf) - 1) {
        argbuf[used + j] = s[j];
        j++;
      }
      argbuf[used + j] = 0;
      kargv[argc] = &argbuf[used];
      used += j + 1;
      argc++;
    }
  }
  if (argc == 0) {
    argbuf[0] = 0;
    kargv[0] = argbuf;
    argc = 1;
  }

  uint32_t entry;
  if (elf_load_inode(processes[current].pdir, f->ip, &entry) < 0) {
    fileclose(f);
    return -ENOEXEC;
  }
  fileclose(f);

  mmap_reset();
  uint32_t user_stack = user_build_stack(kargv, argc);
  patch_user_frame(entry, user_stack);
  processes[current].user_code = entry;
  processes[current].user_stack = user_stack;
  return 0;
}

/* Reload the embedded shell after a program it exec'd has exited. */
int process_restore_shell(void) {
  uint32_t entry;
  uint32_t size = (uint32_t)(_binary_build_user_embedded_elf_end -
                             _binary_build_user_embedded_elf_start);
  if (elf_load(processes[current].pdir, _binary_build_user_embedded_elf_start,
               size, &entry) < 0)
    return -1;
  mmap_reset();
  static const char *argv[] = { "muxsh", 0 };
  uint32_t user_stack = user_build_stack(argv, 1);
  patch_user_frame(entry, user_stack);
  processes[current].user_code = entry;
  processes[current].user_stack = user_stack;
  return 0;
}

int process_current_pid() { return current; }
int process_get_info(uint32_t pid, process_info_t *info) {
  for (int i = 0; i < process_count; i++) {
    if (processes[i].pid == pid) {
      info->exit_code = processes[i].exit_code;
      kstrcpy(info->name, processes[i].process_name);
      info->parent_pid = processes[i].parent_pid;
      info->state = processes[i].state;
      info->pid = processes[i].pid;
      return 0;
    }
  }
  return -1;
}

int process_get_count(void) { return process_count; }
