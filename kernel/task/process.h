#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#define MAX_PROCESSES 64
#define PROC_RUNNING 1
#define PROC_ZOMBIE 2 // 已退出但父进程还没 wait

/* Anonymous mmap regions are handed out from here, per process. */
#define USER_MMAP_BASE 0x30000000u

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

/* Saved user context captured when a signal handler is entered. */
typedef struct {
  uint32_t eip, cs, eflags, esp, ss;
  uint32_t regs[8]; /* edi,esi,ebp,esp,ebx,edx,ecx,eax */
} sigcontext_t;

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
  uint32_t pdir;            // physical address of this process's page directory
  uint32_t mmap_next;       // next free address for anonymous mmap
  uint32_t tls_base;        // %gs base (mlibc's TCB), restored on switch

  uint32_t pgid;            // process group id
  uint32_t sid;             // session id
  uint32_t sig_handler[32]; // 0=SIG_DFL, 1=SIG_IGN, else user handler address
  uint32_t sig_pending;     // pending signal bitmask
  uint32_t sig_blocked;     // blocked signal bitmask
  uint32_t in_signal;       // currently running a handler (no nesting)
  sigcontext_t sig_saved;   // context to restore on sigreturn
  uint32_t syscall_esp;     // this process's iret frame on its kernel stack
} process_t;

_Static_assert(sizeof(process_t) == 464, "update PROCESS_SIZE in switch.s");

/* Signal numbers used by the kernel (match Linux/mlibc). */
#define SIGKILL 9
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGURG 23
#define SIGWINCH 28
#define SIGINT 2
#define SIGQUIT 3
#define SIGHUP 1
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
int process_create_user(void);
void process_register_current();
void start_user_process(int pid, char *process_name);
void process_sleep(uint32_t ticks);
void process_exit();
int process_fork(uint32_t child_eax_ret);
int process_execve(const char *path, const char *const *uargv,
                   const char *const *uenvp);
void process_set_tls(process_t *p);
void mmap_reset(void);
int process_wait();
int process_waitpid(int pid, int flags, int *status);

/* Signals, process groups and sessions */
void process_map_sigtramp(uint32_t pdir);
int signal_deliver(void);
int process_sigreturn(void);
int process_sigaction(int sig, const void *act, void *old);
int process_kill(int pid, int sig);
int process_setpgid(int pid, int pgid);
int process_getpgid(int pid);
int process_getsid(int pid);
int process_setsid(void);
extern int foreground_pgid;
int process_current_pid();
int process_get_info(uint32_t pid, process_info_t *info);
int process_get_count(void);
extern process_t processes[MAX_PROCESSES];
extern int process_count;
extern int current;
#endif
