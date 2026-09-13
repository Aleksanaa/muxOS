#include "process.h"
#include "../lib/string.h"
#include "elf.h"
#include "fs.h"
#include "gdt.h"
#include "pmm.h"
#include "syscall.h"
#include "tss.h"
#include "vga.h"
#include "vmm.h"
#include <stdint.h>
process_t processes[MAX_PROCESSES];
int current = 0;
int process_count = 0;

/*
 * PIDs are monotonic and independent of the slot index.  Reaping a child
 * compacts the process array, so using the index as the pid would silently
 * renumber live processes; a shell waiting on a recorded pid would then wait
 * on the wrong process (or get ECHILD).
 */
static uint32_t next_pid = 1;

static uint32_t alloc_pid(void) { return next_pid++; }

extern void enter_usermode(uint32_t entry, uint32_t stack);

#define USER_STACK_TOP 0x28000000u
#define USER_STACK_PAGES 16u

void context_switch(context_t *old, context_t *new);
void process_enter(context_t *old, context_t *new);
void process_jump(context_t *new);

/* Point %gs at the process's TCB.  Called from the asm switch stubs too. */
void process_set_tls(process_t *p) { gdt_set_tls_base(p->tls_base); }

/*
 * Lay out argc/argv/envp and a minimal auxv at the top of the user stack,
 * newest at the lowest address.  The stack pages must already be mapped.
 */
static uint32_t user_build_stack(const char *const *args, int argc,
                                 const char *const *envp, int envc) {
  uint32_t sp = USER_STACK_TOP;
  uint32_t argp[32];
  uint32_t envpp[32];

  if (argc > 32)
    argc = 32;
  if (envc > 32)
    envc = 32;
  for (int i = 0; i < argc; i++) {
    uint32_t len = kstrlen(args[i]) + 1;
    sp -= len;
    kmemcpy((void *)(uintptr_t)sp, args[i], len);
    argp[i] = sp;
    sp &= ~3u;
  }
  for (int i = 0; i < envc; i++) {
    uint32_t len = kstrlen(envp[i]) + 1;
    sp -= len;
    kmemcpy((void *)(uintptr_t)sp, envp[i], len);
    envpp[i] = sp;
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
  for (int i = envc - 1; i >= 0; i--) {
    sp -= 4;
    *(uint32_t *)(uintptr_t)sp = envpp[i];
  }
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
  processes[0].mmap_next = USER_MMAP_BASE;
  processes[0].tls_base = 0;
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

  process_set_tls(&processes[next]);
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

  kmemset(&processes[process_count], 0, sizeof(process_t));
  processes[process_count].pid = alloc_pid();
  processes[process_count].ctx.esp = stack_top;
  processes[process_count].ctx.ebp = 0;
  processes[process_count].ctx.ebx = 0;
  processes[process_count].ctx.esi = 0;
  processes[process_count].ctx.edi = 0;
  processes[process_count].started = 0;
  processes[process_count].kernel_stack = 0;
  processes[process_count].state = PROC_RUNNING;
  processes[process_count].pdir = vmm_kernel_pdir();
  processes[process_count].mmap_next = USER_MMAP_BASE;
  processes[process_count].tls_base = 0;
  kstrcpy(processes[process_count].process_name, "kernel_init");
  fd_init(processes[process_count].fds);
  process_count++;
}

/* Environment handed to the initial shell (and inherited by everything). */
static const char *init_envp[] = {
    "PATH=/bin",
    "HOME=/",
    "PWD=/",
    "TERM=muxos",
    0,
};

/*
 * Load the initial user process from an ELF stored in the filesystem (i.e.
 * /bin/sh) rather than an image embedded in the kernel.
 */
int process_create_user(void) {
  extern void print(const char *, unsigned char);
  uint32_t entry = 0;

  struct file *f = vfs_open("/bin/sh", O_RDONLY);
  if (!f) {
    print("cannot open /bin/sh\n", 0x0C);
    return -1;
  }

  uint32_t pdir = vmm_create_pdir();
  if (!pdir) {
    fileclose(f);
    print("pdir alloc failed\n", 0x0C);
    return -1;
  }
  if (elf_load_inode(pdir, f->ip, &entry) < 0) {
    fileclose(f);
    print("elf load failed\n", 0x0C);
    return -1;
  }
  fileclose(f);
  process_map_sigtramp(pdir);

  uint32_t stack_base = USER_STACK_TOP - USER_STACK_PAGES * 4096u;
  for (uint32_t i = 0; i < USER_STACK_PAGES; i++) {
    if (!vmm_alloc_at(pdir, stack_base + i * 4096)) {
      print("user stack map failed\n", 0x0C);
      return -1;
    }
  }

  /* Build the stack through the new address space. */
  uint32_t old_pdir = vmm_current_pdir();
  vmm_switch_pdir(pdir);
  static const char *init_argv[] = { "sh", 0 };
  uint32_t user_stack = user_build_stack(init_argv, 1, init_envp, 4);
  vmm_switch_pdir(old_pdir);

  /* 内核栈必须在内核区（无 PAGE_USER），不能用 vmm_alloc */
  uint32_t kernel_stack = pmm_alloc();
  if (!kernel_stack)
    return -1;
  kernel_stack += 4096;

  kmemset(&processes[process_count], 0, sizeof(process_t));
  uint32_t pid = alloc_pid();
  processes[process_count].pid = pid;
  processes[process_count].pgid = pid;
  processes[process_count].sid = pid;
  foreground_pgid = (int)pid;
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
  processes[process_count].mmap_next = USER_MMAP_BASE;
  processes[process_count].tls_base = 0;
  fd_init(processes[process_count].fds);
  process_count++;
  return (int)pid;
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

  process_set_tls(&processes[current]);
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

/* Free a zombie's resources and remove it from the process table. */
static int reap_child(int i) {
  int pid = processes[i].pid;
  uint32_t cpdir = processes[i].pdir;
  if (processes[i].kernel_stack)
    pmm_free(processes[i].kernel_stack - 4096);
  if (cpdir && cpdir != vmm_kernel_pdir())
    vmm_destroy_pdir(cpdir);
  for (int j = i; j < process_count - 1; j++)
    processes[j] = processes[j + 1];
  process_count--;
  if (current > i)
    current--;
  if (current >= process_count)
    current = 0;
  return pid;
}

// Reap a zombie child. Returns child pid, or -1 if no zombie child exists.
// If no zombie but has children, sleeps briefly so scheduler can run children.
int process_wait() {
  uint32_t my_pid = processes[current].pid;
  for (int i = 0; i < process_count; i++) {
    if (processes[i].parent_pid == my_pid &&
        processes[i].state == PROC_ZOMBIE)
      return reap_child(i);
  }
  processes[current].sleep_ticks = 10;
  return -1;
}

/*
 * POSIX-ish waitpid.  `pid` may be a specific child (the common case for a
 * shell) or <= 0 for "any child".  Returns the reaped pid, 0 if WNOHANG and
 * nothing is ready, -ECHILD if there are no matching children, or -EAGAIN so
 * the caller can yield and retry.  WUNTRACED/WCONTINUED are not implemented.
 */
#define WNOHANG_K 1
int process_waitpid(int pid, int flags, int *status) {
  uint32_t my_pid = processes[current].pid;
  int have_child = 0;

  for (int i = 0; i < process_count; i++) {
    if (processes[i].parent_pid != my_pid)
      continue;
    if (pid > 0 && (int)processes[i].pid != pid)
      continue;
    if (processes[i].state == PROC_ZOMBIE) {
      if (status)
        *status = (int)((processes[i].exit_code & 0xFF) << 8); /* WIFEXITED */
      return reap_child(i);
    }
    have_child = 1;
  }

  if (!have_child)
    return -ECHILD;
  if (flags & WNOHANG_K)
    return 0;
  return -EAGAIN;
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
  c->pid = alloc_pid();
  c->parent_pid = parent->pid;
  c->pdir = cpdir;
  c->kernel_stack = cktop;
  c->user_code = parent->user_code;
  c->user_stack = parent->user_stack;
  c->mmap_next = parent->mmap_next;
  c->tls_base = parent->tls_base;
  c->pgid = parent->pgid;
  c->sid = parent->sid;
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
  return (int)c->pid;
}

/*
 * Replace the current process image with the ELF stored at `path`.  argv is
 * snapshotted first because loading the new image overwrites user memory.
 * On success the pending syscall return points at the new entry point and
 * this returns 0; on failure it returns a negative errno.
 */
/* Copy a user string vector into kernel buffers before we overwrite user mem. */
static int snapshot_vec(const char *const *uvec, char *buf, uint32_t bufsz,
                        const char **kvec, int max) {
  int n = 0;
  uint32_t used = 0;
  if (uvec) {
    while (n < max && uvec[n] && used < bufsz - 1) {
      const char *s = uvec[n];
      uint32_t j = 0;
      while (s[j] && used + j < bufsz - 1) {
        buf[used + j] = s[j];
        j++;
      }
      buf[used + j] = 0;
      kvec[n] = &buf[used];
      used += j + 1;
      n++;
    }
  }
  return n;
}

int process_execve(const char *path, const char *const *uargv,
                   const char *const *uenvp) {
  struct file *f = vfs_open(path, O_RDONLY);
  if (!f)
    return -fs_errno;
  if (f->ip->type != T_FILE) {
    fileclose(f);
    return -EACCES;
  }

  static char argbuf[1024];
  static char envbuf[2048];
  const char *kargv[32];
  const char *kenvp[32];
  int argc = snapshot_vec(uargv, argbuf, sizeof(argbuf), kargv, 32);
  int envc = snapshot_vec(uenvp, envbuf, sizeof(envbuf), kenvp, 32);
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

  process_map_sigtramp(processes[current].pdir);
  mmap_reset();
  uint32_t user_stack = user_build_stack(kargv, argc, kenvp, envc);
  patch_user_frame(entry, user_stack);
  processes[current].user_code = entry;
  processes[current].user_stack = user_stack;
  return 0;
}

/* --- signals, process groups and sessions ------------------------------ */

/* Where the SIGRETURN trampoline is mapped in every user address space. */
#define SIGRETURN_TRAMPOLINE 0x40000000u

int foreground_pgid = 0;

/* Map a tiny "mov eax, SYS_SIGRETURN; int 0x80" stub so a handler can return. */
void process_map_sigtramp(uint32_t pdir) {
  if (vmm_page_present(pdir, SIGRETURN_TRAMPOLINE))
    return;
  if (!vmm_alloc_at(pdir, SIGRETURN_TRAMPOLINE))
    return;
  uint8_t code[7] = {0xB8, (uint8_t)(SYS_SIGRETURN & 0xFF), 0, 0, 0, 0xCD, 0x80};
  uint32_t phys = vmm_phys(pdir, SIGRETURN_TRAMPOLINE);
  if (phys)
    kmemcpy((void *)(uintptr_t)phys, code, sizeof(code));
}

int process_sigaction(int sig, const void *act, void *old) {
  if (sig <= 0 || sig >= 64)
    return -EINVAL;
  process_t *p = &processes[current];
  /* Signals >= 32 (e.g. mlibc's SIGCANCEL) are accepted but not delivered. */
  if (sig < 32) {
    if (old)
      *(uint32_t *)old = p->sig_handler[sig];
    if (act)
      p->sig_handler[sig] = *(const uint32_t *)act;
  } else if (old) {
    *(uint32_t *)old = 0;
  }
  return 0;
}

int process_kill(int pid, int sig) {
  if (sig <= 0 || sig >= 64)
    return -EINVAL;
  if (sig >= 32)
    return 0; /* ignored */
  process_t *me = &processes[current];
  int sent = 0;

  for (int i = 0; i < process_count; i++) {
    process_t *t = &processes[i];
    if (t->pid == 0)
      continue;
    int match;
    if (pid > 0)
      match = ((int)t->pid == pid);
    else if (pid == 0)
      match = (t->pgid == me->pgid);
    else
      match = (t->pgid == (uint32_t)(-pid));
    if (match) {
      t->sig_pending |= (1u << sig);
      sent = 1;
    }
  }
  return sent ? 0 : -ESRCH;
}

/* Restore the context saved when a handler was entered.  Returns the saved
 * eax so the syscall stub writes it back (it otherwise clobbers eax). */
int process_sigreturn(void) {
  process_t *p = &processes[current];
  uint32_t *iret = (uint32_t *)(uintptr_t)syscall_kernel_esp;
  uint32_t *regs = iret - 8;

  iret[0] = p->sig_saved.eip;
  iret[1] = p->sig_saved.cs;
  iret[2] = p->sig_saved.eflags;
  iret[3] = p->sig_saved.esp;
  iret[4] = p->sig_saved.ss;
  for (int i = 0; i < 8; i++)
    regs[i] = p->sig_saved.regs[i];
  p->in_signal = 0;
  return (int)p->sig_saved.regs[7];
}

/*
 * Deliver one pending signal to the current process, just before it returns to
 * user mode.  Returns 1 if a handler was entered (the iret frame now points at
 * it), 0 otherwise.  Default actions may terminate the process (no return).
 */
int signal_deliver(void) {
  process_t *p = &processes[current];
  if (p->in_signal)
    return 0;

  uint32_t pend = p->sig_pending & ~p->sig_blocked;
  if (!pend)
    return 0;

  for (int sig = 1; sig < 32; sig++) {
    if (!(pend & (1u << sig)))
      continue;
    p->sig_pending &= ~(1u << sig);

    uint32_t h = p->sig_handler[sig];
    if (h == 1) /* SIG_IGN */
      continue;

    if (h == 0) {
      /* SIG_DFL */
      if (sig == SIGCHLD || sig == SIGCONT || sig == SIGURG ||
          sig == SIGWINCH || sig == SIGTSTP || sig == SIGTTIN ||
          sig == SIGTTOU || sig == SIGSTOP)
        continue; /* ignored / stop unsupported */
      p->exit_code = 128 + (uint32_t)sig;
      process_exit();
      return 1; /* not reached */
    }

    uint32_t *iret = (uint32_t *)(uintptr_t)syscall_kernel_esp;
    uint32_t *regs = iret - 8;
    p->sig_saved.eip = iret[0];
    p->sig_saved.cs = iret[1];
    p->sig_saved.eflags = iret[2];
    p->sig_saved.esp = iret[3];
    p->sig_saved.ss = iret[4];
    for (int r = 0; r < 8; r++)
      p->sig_saved.regs[r] = regs[r];

    /* handler(sig): push the return address and the argument. */
    uint32_t usp = iret[3] - 8;
    *(uint32_t *)(uintptr_t)(usp + 4) = (uint32_t)sig;
    *(uint32_t *)(uintptr_t)usp = SIGRETURN_TRAMPOLINE;
    iret[0] = h;   /* EIP */
    iret[3] = usp; /* ESP_user */
    p->in_signal = 1;
    return 1;
  }
  return 0;
}

int process_setpgid(int pid, int pgid) {
  if (pid == 0)
    pid = (int)processes[current].pid;
  if (pgid == 0)
    pgid = pid;
  for (int i = 0; i < process_count; i++) {
    if ((int)processes[i].pid == pid) {
      processes[i].pgid = (uint32_t)pgid;
      return 0;
    }
  }
  return -ESRCH;
}

int process_getpgid(int pid) {
  if (pid == 0)
    pid = (int)processes[current].pid;
  for (int i = 0; i < process_count; i++)
    if ((int)processes[i].pid == pid)
      return (int)processes[i].pgid;
  return -ESRCH;
}

int process_getsid(int pid) {
  if (pid == 0)
    pid = (int)processes[current].pid;
  for (int i = 0; i < process_count; i++)
    if ((int)processes[i].pid == pid)
      return (int)processes[i].sid;
  return -ESRCH;
}

int process_setsid(void) {
  process_t *p = &processes[current];
  p->sid = p->pid;
  p->pgid = p->pid;
  foreground_pgid = (int)p->pid;
  return (int)p->pid;
}

int process_current_pid() { return (int)processes[current].pid; }
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
