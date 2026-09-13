/* muxOS shell.
 *
 * Only real builtins live here (cd/exit/help); every other command is looked
 * up in /bin and launched with execve(), which replaces this process.  When
 * the program exits the kernel reloads the shell.
 */

#include "fs_uapi.h"
#include "kernel.h"
#include "syscall.h"
#include "userlib.h"
#include <stdint.h>

static void puts(const char *s) { sys_write(1, s, (int)strlen(s)); }
static void putc_(char c) { sys_write(1, &c, 1); }

static int read_line(char *buf, int max) {
  int n = 0;
  while (n < max - 1) {
    char c = sys_getchar();
    if (c == '\r')
      continue;
    if (c == '\n') {
      putc_('\n');
      break;
    }
    if (c == '\b') {
      if (n > 0) {
        n--;
        sys_vgaspace();
      }
      continue;
    }
    buf[n++] = c;
    putc_(c);
  }
  buf[n] = 0;
  return n;
}

static int tokenize(char *line, char **argv, int max) {
  int argc = 0;
  char *p = line;
  while (*p && argc < max) {
    while (*p == ' ' || *p == '\t')
      p++;
    if (!*p)
      break;
    argv[argc++] = p;
    while (*p && *p != ' ' && *p != '\t')
      p++;
    if (*p)
      *p++ = 0;
  }
  return argc;
}

static void cmd_cd(const char *path) {
  if (sys_chdir(path) < 0) {
    puts("cd: ");
    puts(path);
    puts(": not a directory\n");
  }
}

/* Run /bin/<cmd>, or <cmd> directly when it contains a slash. */
static void cmd_exec(char **argv) {
  char path[320];
  const char *cmd = argv[0];

  if (strchr(cmd, '/')) {
    strncpy(path, cmd, sizeof(path) - 1);
  } else {
    path[0] = '/';
    path[1] = 'b';
    path[2] = 'i';
    path[3] = 'n';
    path[4] = '/';
    strncpy(path + 5, cmd, sizeof(path) - 6);
  }
  path[sizeof(path) - 1] = 0;

  int pid = sys_fork();
  if (pid < 0) {
    puts("muxsh: fork failed\n");
    return;
  }
  if (pid == 0) {
    sys_execve(path, argv); /* only returns on failure */
    puts("muxsh: ");
    puts(cmd);
    puts(": not found\n");
    sys_exit();
    for (;;)
      ;
  }
  /* Stay resident; reap the child before prompting again. */
  while (sys_wait() < 0)
    ;
}

static void usage(void) {
  puts("builtins: cd <dir> | exit | help\n");
  puts("everything else runs from /bin (ls, cat, cp, mv, rm, mkdir, ...)\n");
}

void init_shell() {
  char line[256];
  char *argv[17];

  puts("Welcome to MuxOS!\n");
  usage();

  for (;;) {
    puts("muxOS> ");
    if (read_line(line, sizeof(line)) == 0)
      continue;

    int argc = tokenize(line, argv, 16);
    if (argc == 0)
      continue;
    argv[argc] = 0; /* execve takes a NULL-terminated vector */
    const char *cmd = argv[0];

    if (strcmp(cmd, "exit") == 0) {
      puts("bye\n");
      sys_shutdown();
      continue;
    } else if (strcmp(cmd, "help") == 0) {
      usage();
    } else if (strcmp(cmd, "cd") == 0) {
      cmd_cd(argc > 1 ? argv[1] : "/");
    } else {
      cmd_exec(argv);
    }
  }
}
