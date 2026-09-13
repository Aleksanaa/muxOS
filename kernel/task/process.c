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

extern void enter_usermode(uint32_t entry, uint32_t stack);

/* The userland ELF is embedded in the kernel image by objcopy. */
extern const uint8_t _binary_build_user_embedded_elf_start[];
extern const uint8_t _binary_build_user_embedded_elf_end[];

#define USER_STACK_TOP 0x28000000u
#define USER_STACK_PAGES 16u

void context_switch(context_t *old, context_t *new);
void process_enter(context_t *old, context_t *new);
void process_jump(context_t *new);

void process_register_current() {
  processes[0].pid = 0;
  processes[0].started = 1;
  processes[0].kernel_stack = 0;
  processes[process_count].state = PROC_RUNNING;
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
  kstrcpy(processes[process_count].process_name, "kernel_init");
  fd_init(processes[process_count].fds);
  process_count++;
}

void process_create_user(void) {
  extern void print(const char *, unsigned char);
  uint32_t entry = 0;
  uint32_t elf_size = (uint32_t)(_binary_build_user_embedded_elf_end -
                                 _binary_build_user_embedded_elf_start);
  if (elf_load(_binary_build_user_embedded_elf_start, elf_size, &entry) < 0) {
    print("elf load failed\n", 0x0C);
    return;
  }

  uint32_t stack_base = USER_STACK_TOP - USER_STACK_PAGES * 4096u;
  for (uint32_t i = 0; i < USER_STACK_PAGES; i++) {
    if (!vmm_alloc_at(stack_base + i * 4096)) {
      print("user stack map failed\n", 0x0C);
      return;
    }
  }
  /* Build the initial process stack: argc/argv/envp + a minimal auxv.
   * mlibc's startup walks the auxv, so AT_NULL must be present or it reads
   * off the top of the stack. */
  uint32_t sp = USER_STACK_TOP;
  const char *arg0 = "init";
  sp -= 5;
  kmemcpy((void *)(uintptr_t)sp, arg0, 5); // "init\0"
  uint32_t arg0p = sp;
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
  sp -= 4; // argv[0]
  *(uint32_t *)(uintptr_t)sp = arg0p;
  sp -= 4; // argc
  *(uint32_t *)(uintptr_t)sp = 1;
  uint32_t user_stack = sp;

  /* 内核栈必须在内核区（无 PAGE_USER），不能用 vmm_alloc */
  uint32_t kernel_stack = pmm_alloc();
  if (!kernel_stack)
    return;
  kernel_stack += 4096;

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

  /* 无父进程：直接释放内存并删除。
   * 注意：ELF 映射的代码/数据页未跟踪，此处不释放（toy 阶段接受泄漏）。 */
  if (p->kernel_stack != 0) {
    if (p->user_stack) {
      for (uint32_t i = 0; i < USER_STACK_PAGES; i++)
        vmm_free(p->user_stack - 4096 * (i + 1));
    }
    pmm_free(p->kernel_stack - 4096);
  }

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
      // free child's memory (fork child has 1 stack page)
      if (processes[i].user_code)
        vmm_free(processes[i].user_code);
      if (processes[i].user_stack)
        vmm_free(processes[i].user_stack - 4096);
      if (processes[i].kernel_stack)
        pmm_free(processes[i].kernel_stack - 4096);
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
  /*
   * TODO: copy the process address space (ELF segments + stack).  The kernel
   * still runs everything on one shared page directory, so a correct fork is
   * not possible yet; this becomes necessary alongside mlibc's fork/exec.
   */
  return -1;
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
