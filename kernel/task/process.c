#include "process.h"
#include "../lib/string.h"
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
extern char _user_load_start;
extern char _user_load_end;
extern char _user_start;
extern char _user_end;
extern char _user_text_load_start;
extern char _user_text_load_end;
extern char _user_rodata;
extern char _user_rodata_load_start;
extern char _user_rodata_load_end;

#define USER_BASE 0x20000000u

static uint32_t user_image_pages() {
  uint32_t image_size = (uint32_t)&_user_end - (uint32_t)&_user_start;
  return (image_size + 4095) / 4096;
}

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

void process_create_user(void (*entry)()) {
  extern void print(const char *, unsigned char);
  uint32_t image_pages = user_image_pages();
  uint32_t code_page = USER_BASE;
  for (uint32_t i = 0; i < image_pages; i++) {
    if (!vmm_alloc_at(USER_BASE + i * 4096)) {
      for (uint32_t j = 0; j < i; j++)
        vmm_free(USER_BASE + j * 4096);
      return;
    }
  }

  uint8_t *text_src = (uint8_t *)&_user_text_load_start;
  uint8_t *text_dst = (uint8_t *)USER_BASE;
  for (uint32_t i = 0;
       i < (uint32_t)&_user_text_load_end - (uint32_t)&_user_text_load_start;
       i++)
    text_dst[i] = text_src[i];

  uint8_t *rodata_src = (uint8_t *)&_user_rodata_load_start;
  uint8_t *rodata_dst = (uint8_t *)&_user_rodata;
  for (uint32_t i = 0; i < (uint32_t)&_user_rodata_load_end -
                               (uint32_t)&_user_rodata_load_start;
       i++)
    rodata_dst[i] = rodata_src[i];

  // 固定布局：代码段位于 USER_BASE，用户栈位于 USER_BASE + 64KiB 之后，
  // 避免 vmm_alloc() 在用户地址空间中分配到错误的低地址页，并造成
  // 代码页/栈页发生重叠或错位。
  uint32_t user_stack_base = USER_BASE + 0x10000u;
  for (uint32_t i = 0; i < 4; i++) {
    if (!vmm_alloc_at(user_stack_base + i * 4096)) {
      for (uint32_t j = 0; j < image_pages; j++)
        vmm_free(USER_BASE + j * 4096);
      for (uint32_t j = 0; j < i; j++)
        vmm_free(user_stack_base + j * 4096);
      return;
    }
  }
  /* 栈向低地址增长，栈顶应位于四个已映射页之后。 */
  uint32_t user_stack = user_stack_base + 4 * 4096;

  /* 内核栈必须在内核区（无 PAGE_USER），不能用 vmm_alloc */
  uint32_t kernel_stack = pmm_alloc();
  if (!kernel_stack) {
    return;
  }
  kernel_stack += 4096;

  processes[process_count].pid = process_count;
  processes[process_count].ctx.esp = code_page;
  processes[process_count].ctx.ebp = user_stack;
  processes[process_count].ctx.ebx = 0;
  processes[process_count].ctx.esi = 0;
  processes[process_count].ctx.edi = 0;
  processes[process_count].started = 0;
  processes[process_count].kernel_stack = kernel_stack;
  processes[process_count].user_code = code_page;
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

  /* 无父进程：直接释放内存并删除 */
  if (p->kernel_stack != 0) {
    if (p->user_code) {
      for (uint32_t i = 0; i < user_image_pages(); i++)
        vmm_free(p->user_code + i * 4096);
    }
    if (p->user_stack) {
      for (int i = 0; i < 4; i++)
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
  extern void print(const char *, unsigned char);
  print("fork start\n", 0x0E);
  process_t *p = &processes[current];
  uint32_t parent_pid = p->pid;
  if (process_count >= MAX_PROCESSES)
    return -1;

  // allocate new code page and copy user code
  extern void user_c_start();
  extern void user_c_end();
  uint32_t start = (uint32_t)&user_c_start;
  uint32_t end = (uint32_t)&user_c_end;
  uint32_t code_size = end - start;
  if (code_size > 4096)
    code_size = 4096;

  uint32_t code_page = vmm_alloc();
  if (!code_page)
    return -1;
  uint8_t *src = (uint8_t *)start;
  uint8_t *dst = (uint8_t *)code_page;
  for (uint32_t i = 0; i < code_size; i++)
    dst[i] = src[i];

  // allocate new user stack (1 page) and copy parent's top stack page
  uint32_t child_user_stack_base = vmm_alloc();
  if (!child_user_stack_base)
    return -1;
  uint32_t child_user_stack_top = child_user_stack_base + 4096;

  // copy parent's top stack page (where the active stack frame lives)
  uint32_t parent_top_page = p->user_stack - 4096;
  uint8_t *usrc = (uint8_t *)(uintptr_t)parent_top_page;
  uint8_t *udst = (uint8_t *)(uintptr_t)child_user_stack_base;
  for (int i = 0; i < 4096; i++)
    udst[i] = usrc[i];

  // allocate new kernel stack and copy parent's kernel stack
  uint32_t parent_kstack_base = processes[current].kernel_stack - 4096;
  uint32_t child_kstack = pmm_alloc();
  if (!child_kstack)
    return -1;
  uint8_t *ksrc = (uint8_t *)(uintptr_t)parent_kstack_base;
  uint8_t *kdst = (uint8_t *)(uintptr_t)child_kstack;
  for (int i = 0; i < 4096; i++)
    kdst[i] = ksrc[i];
  uint32_t child_kstack_top = child_kstack + 4096;

  // compute child's kernel esp: same offset from stack base as parent
  uint32_t esp_offset = processes[current].kernel_stack - syscall_kernel_esp;
  uint32_t child_iret_esp = child_kstack_top - esp_offset;

  // syscall_stub now does pusha before saving syscall_kernel_esp? No:
  // syscall_kernel_esp is saved BEFORE pusha, so:
  //   child_iret_esp = child_kstack_top - (kernel_stack - syscall_kernel_esp)
  //   parent pusha frame is at syscall_kernel_esp - 32
  //   child pusha frame is at child_iret_esp - 32
  uint32_t child_pusha_esp = child_iret_esp - 32;

  // copy parent's pusha frame (contains real user registers including ebp)
  uint32_t parent_pusha_esp = syscall_kernel_esp - 32;
  uint32_t *src_pusha = (uint32_t *)(uintptr_t)parent_pusha_esp;
  uint32_t *dst_pusha = (uint32_t *)(uintptr_t)child_pusha_esp;
  for (int i = 0; i < 8; i++)
    dst_pusha[i] = src_pusha[i];
  // patch eax = child return value (0)
  dst_pusha[7] = child_eax_ret;

  uint32_t child_esp = child_pusha_esp;

  // patch iret frame: update ESP_user to child's stack
  uint32_t *child_iret = (uint32_t *)(uintptr_t)child_iret_esp;
  uint32_t parent_user_esp = child_iret[3];
  uint32_t offset_from_top = p->user_stack - parent_user_esp;
  child_iret[3] = child_user_stack_top - offset_from_top;

  int child_idx = process_count;
  processes[child_idx].pid = process_count + 1;
  processes[child_idx].ctx.esp = child_esp;
  processes[child_idx].ctx.ebp = 0;
  processes[child_idx].ctx.ebx = 0;
  processes[child_idx].ctx.esi = 0;
  processes[child_idx].ctx.edi = 0;
  processes[child_idx].state = 1;
  processes[child_idx].started = 1;
  processes[child_idx].kernel_stack = child_kstack_top;
  processes[child_idx].sleep_ticks = 0;
  processes[child_idx].user_code = code_page;
  processes[child_idx].user_stack = child_user_stack_top;
  processes[current].state = PROC_RUNNING;
  processes[child_idx].parent_pid = parent_pid;
  fd_fork(processes[current].fds, processes[child_idx].fds);
  process_count++;

  return processes[child_idx].pid;
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
