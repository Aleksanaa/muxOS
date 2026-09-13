
#include "fs_uapi.h"
#include "kernel.h"
#include "syscall.h"
#include "userlib.h"
#include <stdint.h>

static void puts(const char *s) { sys_write(1, s, (int)strlen(s)); }
static void putc_(char c) { sys_write(1, &c, 1); }

/* Pointer to the start of the n-th whitespace separated argument. */
static char *skip_args(char *line, int n) {
  char *p = line;
  for (int i = 0; i < n; i++) {
    while (*p == ' ' || *p == '\t')
      p++;
    while (*p && *p != ' ' && *p != '\t')
      p++;
  }
  while (*p == ' ' || *p == '\t')
    p++;
  return p;
}

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

static void cmd_ls(const char *path) {
  int fd = sys_open(path, O_RDONLY);
  if (fd < 0) {
    puts("ls: cannot open ");
    puts(path);
    puts("\n");
    return;
  }
  struct dirent de;
  while (sys_getdents(fd, &de, (int)sizeof(de)) > 0) {
    puts(de.name);
    if (de.type == T_DIR)
      putc_('/');
    putc_('\n');
  }
  sys_close(fd);
}

static void cmd_cat(const char *path) {
  int fd = sys_open(path, O_RDONLY);
  if (fd < 0) {
    puts("cat: cannot open ");
    puts(path);
    puts("\n");
    return;
  }
  char buf[256];
  int n;
  while ((n = sys_read(fd, buf, sizeof(buf))) > 0)
    sys_write(1, buf, n);
  sys_close(fd);
}

static void cmd_write(const char *path, const char *text, int append) {
  int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
  int fd = sys_open(path, flags);
  if (fd < 0) {
    puts("write: cannot open ");
    puts(path);
    puts("\n");
    return;
  }
  sys_write(fd, text, (int)strlen(text));
  sys_write(fd, "\n", 1);
  sys_close(fd);
}

static void cmd_stat(const char *path) {
  struct stat st;
  char num[16];
  if (sys_stat(path, &st) < 0) {
    puts("stat: no such file\n");
    return;
  }
  puts("ino ");
  itoa((int)st.ino, num);
  puts(num);
  puts("  type ");
  if (st.type == T_DIR)
    puts("dir");
  else if (st.type == T_DEVICE)
    puts("dev");
  else
    puts("file");
  puts("  size ");
  itoa((int)st.size, num);
  puts(num);
  puts("  nlink ");
  itoa((int)st.nlink, num);
  puts(num);
  putc_('\n');
}

static void cmd_ps(void) {
  int count = sys_get_process_count();
  puts("pid ppid status name\n");
  for (int i = 0; i < count; i++) {
    process_info_t info = {0};
    sys_get_process_info(i, &info);
    char num[16];
    itoa((int)info.pid, num);
    puts(num);
    puts("  ");
    if (info.parent_pid) {
      itoa((int)info.parent_pid, num);
      puts(num);
    } else {
      puts("N/A");
    }
    puts("  ");
    puts(info.state == 2 ? "ZOMBIE" : "RUNNING");
    puts("  ");
    puts(info.name);
    putc_('\n');
  }
}

static void usage(void) {
  puts("fs : ls [path] | cat <file> | write <file> <text> | append <file> <text>\n");
  puts("     mkdir <dir> | rm <path> | stat <path>\n");
  puts("sys: ps | getpid | clear | version | reboot | shutdown\n");
}

void init_shell() {
  char line[256];
  char *argv[8];
  puts("Welcome to MuxOS!\n");
  usage();
  for (;;) {
    puts("muxOS> ");
    if (read_line(line, sizeof(line)) == 0)
      continue;

    /* write/append need the raw remainder before tokenize() mutates line. */
    if (strncmp(line, "write ", 6) == 0 || strncmp(line, "append ", 7) == 0) {
      int is_append = strncmp(line, "append ", 7) == 0;
      char *path = skip_args(line, 1);
      char *p = path;
      while (*p && *p != ' ' && *p != '\t')
        p++;
      if (*p)
        *p++ = 0;
      while (*p == ' ' || *p == '\t')
        p++;
      if (!*path) {
        puts("usage: write <file> <text>\n");
        continue;
      }
      cmd_write(path, p, is_append);
      continue;
    }

    int argc = tokenize(line, argv, 8);
    if (argc == 0)
      continue;
    const char *cmd = argv[0];

    if (strcmp(cmd, "help") == 0) {
      usage();
    } else if (strcmp(cmd, "ls") == 0) {
      cmd_ls(argc > 1 ? argv[1] : "/");
    } else if (strcmp(cmd, "cat") == 0) {
      if (argc > 1)
        cmd_cat(argv[1]);
      else
        puts("usage: cat <file>\n");
    } else if (strcmp(cmd, "mkdir") == 0) {
      if (argc > 1 && sys_mkdir(argv[1]) == 0)
        puts("ok\n");
      else
        puts("mkdir failed\n");
    } else if (strcmp(cmd, "rm") == 0) {
      if (argc > 1 && sys_unlink(argv[1]) == 0)
        puts("ok\n");
      else
        puts("rm failed\n");
    } else if (strcmp(cmd, "stat") == 0) {
      if (argc > 1)
        cmd_stat(argv[1]);
      else
        puts("usage: stat <path>\n");
    } else if (strcmp(cmd, "ps") == 0) {
      cmd_ps();
    } else if (strcmp(cmd, "getpid") == 0) {
      char num[16];
      itoa(sys_getpid(), num);
      puts(num);
      putc_('\n');
    } else if (strcmp(cmd, "version") == 0) {
      puts(KERNEL_VERSION);
      putc_('\n');
    } else if (strcmp(cmd, "clear") == 0) {
      sys_clear();
    } else if (strcmp(cmd, "reboot") == 0) {
      sys_restart();
    } else if (strcmp(cmd, "shutdown") == 0) {
      sys_shutdown();
    } else {
      puts("unknown command: ");
      puts(cmd);
      putc_('\n');
    }
  }
}
