#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#define MAX_PROCESSES 64
#define PROC_RUNNING 1
#define PROC_ZOMBIE 2 // 已退出但父进程还没 wait

#ifndef FD_MAX
#define FD_MAX 16
#endif

struct file;

typedef struct {
  uint32_t esp;
  uint32_t ebp;
  uint32_t ebx;
  uint32_t esi;
  uint32_t edi;
} context_t;

typedef struct {
  uint32_t pid;
  context_t ctx;
  uint32_t state;
  uint32_t started;
  uint32_t kernel_stack;
  uint32_t sleep_ticks;
  uint32_t user_code;  // code_page 虚拟地址（用于 exec 释放）
  uint32_t user_stack; // 用户栈顶虚拟地址（用于 exec 释放）
  uint32_t parent_pid; // 父进程 pid
  uint32_t exit_code;  // 退出时存在这里
  char process_name[128];
  struct file *fds[FD_MAX]; // per-process open file descriptors
} process_t;

_Static_assert(sizeof(process_t) == 248, "update PROCESS_SIZE in switch.s");
typedef struct {
  uint32_t pid;
  uint32_t parent_pid;
  uint32_t state;
  uint32_t exit_code;
  char name[128];
} process_info_t;
void process_schedule();
int process_tick();
void process_create_kernel(void (*entry)());
void process_create_user(void);
void process_register_current();
void start_user_process(int pid, char *process_name);
void process_sleep(uint32_t ticks);
void process_exit();
int process_fork(uint32_t child_eax_ret);
int process_execve(const char *path, const char *const *uargv);
int process_restore_shell(void);
void mmap_reset(void);
int process_wait();
int process_current_pid();
int process_get_info(uint32_t pid, process_info_t *info);
int process_get_count(void);
extern process_t processes[MAX_PROCESSES];
extern int process_count;
extern int current;
extern uint32_t syscall_kernel_esp;
#endif
