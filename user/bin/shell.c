
#include "console.h"
#include "kernel.h"
#include "process.h"
#include "syscall.h"
#include "userlib.h"
#include <stdatomic.h>
#include <stdint.h>
static void init_shell() {
  char buf[128];
  // sys_clear();
  int count = 0;
  sys_write(1, "Welcome to MuxOS!\n", 19);
  char prefix[128] = "muxOS> ";
  sys_write(STDOUT, prefix, 7);

  while (1) {
    char c = sys_getchar();
    if (count >= 127) {
      count = 0;
    }
    if (c == '\b') {
      if (count == 0) {
        continue;
      }
      if (count > 0) {
        buf[count] = '\0';
        sys_vgaspace();
        count--;
        continue;
      }
    }
    buf[count] = c;
    buf[++count] = '\0';
    if (c == '\n') {
      sys_write(STDOUT, &c, 1);
      if (strcmp(buf, "reboot\n\0") == 0) {
        sys_restart();
      } else if (strcmp(buf, "shutdown\n\0") == 0) {
        sys_shutdown();

      } else if (strncmp(buf, "print\n\0", 6) == 0) {
        sys_write(STDOUT, buf + 6, count - 7);
        sys_write(STDOUT, &c, 1);
      } else if (strcmp(buf, "clear\n\0") == 0) {
        sys_clear();
      } else if (strcmp(buf, "version\n\0") == 0) {
        sys_write(STDOUT, KERNEL_VERSION, 5);
        sys_write(STDOUT, &c, 1);
      } else if (strcmp(buf, "getpid\n\0") == 0) {
        char pid_buf[16];
        int pid = sys_getpid();
        itoa(pid, pid_buf);

        sys_write(STDOUT, pid_buf, 16);
        sys_write(STDOUT, &c, 1);
      } else if (strcmp(buf, "ps\n\0") == 0) {
        int process_count = sys_get_process_count();
        sys_write(STDOUT, "pid ppid status name\n", 21);
        for (int i = 0; i < process_count; i++) {
          process_info_t process_info = {0};
          sys_get_process_info(i, &process_info);
          char pid_buf[16];
          itoa(process_info.pid, pid_buf);
          sys_write(STDOUT, pid_buf, 16);
          sys_write(STDOUT, "  ", 2);
          if (process_info.parent_pid) {
            itoa(process_info.parent_pid, pid_buf);
            sys_write(STDOUT, pid_buf, 16);
            sys_write(STDOUT, "  ", 2);
          } else {
            sys_write(STDOUT, "N/A", 3);
          }
          sys_write(STDOUT, "  ", 2);
          switch (process_info.state) {
          case PROC_RUNNING:
            sys_write(STDOUT, "RUNNING", 8);
            sys_write(STDOUT, "  ", 2);
            break;
          case PROC_ZOMBIE:
            sys_write(STDOUT, "ZOMBILE", 8);
            sys_write(STDOUT, "  ", 2);
            break;
          default:
            sys_write(STDOUT, "UNKNOWN", 8);
            sys_write(STDOUT, "  ", 2);
            break;
          }
          sys_write(STDOUT, process_info.name, 32);
          sys_write(STDOUT, "\n", 1);
        }

      } else {
        sys_write(STDOUT, "UNKNOWN COMMAND.\n", 18);
      }
      count = 0;
      buf[0] = '\0';
      char prefix[128] = "muxOS> ";
      sys_write(STDOUT, prefix, 7);
      continue;
    }
    sys_write(STDOUT, &c, 1);
  }
}