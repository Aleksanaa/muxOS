
#include "console.h"
#include "process.h"
#include "shell.c"
#include "syscall.h"
#include "vga.h"
#include <stdatomic.h>
#include <stdint.h>
/* =========================================================
 * string.h
 * ========================================================= */

int strlen(const char *s) {
  int len = 0;

  while (s[len] != '\0')
    len++;

  return len;
}

char *strcpy(char *dest, const char *src) {
  char *ret = dest;

  while ((*dest++ = *src++) != '\0')
    ;

  return ret;
}

char *strncpy(char *dest, const char *src, int n) {
  int i;

  for (i = 0; i < n && src[i] != '\0'; i++)
    dest[i] = src[i];

  for (; i < n; i++)
    dest[i] = '\0';

  return dest;
}

char *strcat(char *dest, const char *src) {
  char *ret = dest;

  while (*dest)
    dest++;

  while ((*dest++ = *src++) != '\0')
    ;

  return ret;
}

char *strncat(char *dest, const char *src, int n) {
  char *ret = dest;

  while (*dest)
    dest++;

  while (n-- > 0 && *src)
    *dest++ = *src++;

  *dest = '\0';

  return ret;
}

int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }

  return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, int n) {
  while (n > 0 && *a && *a == *b) {
    a++;
    b++;
    n--;
  }

  if (n == 0)
    return 0;

  return (unsigned char)*a - (unsigned char)*b;
}

char *strchr(const char *s, int c) {
  while (*s) {
    if (*s == (char)c)
      return (char *)s;

    s++;
  }

  if (c == '\0')
    return (char *)s;

  return 0;
}

char *strrchr(const char *s, int c) {
  const char *last = 0;

  while (*s) {
    if (*s == (char)c)
      last = s;

    s++;
  }

  if (c == '\0')
    return (char *)s;

  return (char *)last;
}

/* =========================================================
 * string / memory
 * ========================================================= */

void *memset(void *dest, int c, unsigned int n) {
  unsigned char *p = dest;

  while (n--)
    *p++ = (unsigned char)c;

  return dest;
}

void *memcpy(void *dest, const void *src, unsigned int n) {
  unsigned char *d = dest;
  const unsigned char *s = src;

  while (n--)
    *d++ = *s++;

  return dest;
}

void *memmove(void *dest, const void *src, unsigned int n) {
  unsigned char *d = dest;
  const unsigned char *s = src;

  if (d < s) {
    while (n--)
      *d++ = *s++;
  } else if (d > s) {
    d += n;
    s += n;

    while (n--)
      *--d = *--s;
  }

  return dest;
}

int memcmp(const void *a, const void *b, unsigned int n) {
  const unsigned char *p1 = a;
  const unsigned char *p2 = b;

  while (n--) {
    if (*p1 != *p2)
      return *p1 - *p2;

    p1++;
    p2++;
  }

  return 0;
}

/* =========================================================
 * ctype.h
 * ========================================================= */

int isdigit(int c) { return c >= '0' && c <= '9'; }

int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

int isalnum(int c) { return isalpha(c) || isdigit(c); }

int islower(int c) { return c >= 'a' && c <= 'z'; }

int isupper(int c) { return c >= 'A' && c <= 'Z'; }

int isspace(int c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
         c == '\f';
}

int isprint(int c) { return c >= 32 && c <= 126; }

int isxdigit(int c) {
  return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int tolower(int c) {
  if (c >= 'A' && c <= 'Z')
    return c + ('a' - 'A');

  return c;
}

int toupper(int c) {
  if (c >= 'a' && c <= 'z')
    return c - ('a' - 'A');

  return c;
}

/* =========================================================
 * stdlib.h
 * ========================================================= */

int atoi(const char *s) {
  int sign = 1;
  int value = 0;

  while (isspace(*s))
    s++;

  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+') {
    s++;
  }

  while (isdigit(*s)) {
    value = value * 10 + (*s - '0');
    s++;
  }

  return value * sign;
}

int abs(int value) { return value < 0 ? -value : value; }

/* =========================================================
 * integer conversion
 * ========================================================= */

void itoa(int value, char *buf) {
  char tmp[16];
  int i = 0;
  int j = 0;

  if (value == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }

  if (value < 0) {
    buf[j++] = '-';
    value = -value;
  }

  while (value > 0) {
    tmp[i++] = '0' + (value % 10);
    value /= 10;
  }

  while (i > 0)
    buf[j++] = tmp[--i];

  buf[j] = '\0';
}

void utoa(unsigned int value, char *buf) {
  char tmp[16];
  int i = 0;
  int j = 0;

  if (value == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }

  while (value > 0) {
    tmp[i++] = '0' + (value % 10);
    value /= 10;
  }

  while (i > 0)
    buf[j++] = tmp[--i];

  buf[j] = '\0';
}

int sys_write(int fd, const char *buf, int len) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(1), "b"(fd), "c"(buf), "d"(len));
  return ret;
}

void sys_exit() {
  asm volatile("int $0x80" ::"a"(2));
  while (1)
    ;
}

void sys_sleep(int ticks) { asm volatile("int $0x80" ::"a"(3), "b"(ticks)); }

int sys_fork() {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(4));
  return ret;
}
int sys_execve(const char *path, char **argv) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(5), "b"(path), "c"(argv));
  return ret;
}
int sys_wait() {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(6));
  return ret;
}

int sys_read(int fd, void *buf, unsigned int count) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(0), "b"(fd), "c"(buf), "d"(count));
  return ret;
}

int sys_restart(void) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(7));
  return ret;
}

int sys_shutdown(void) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(16));
  return ret;
}

int sys_clear(void) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(18));
  return ret;
}
int sys_getchar(void) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_GETCHAR));
  return ret;
}
int sys_vgaspace(void) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_VGASPACE));
  return ret;
}
int sys_getpid(void) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_GETPID));
  return ret;
}

int sys_get_process_info(uint32_t pid, process_info_t *info) {
  int ret;
  asm volatile("int $0x80"
               : "=a"(ret)
               : "a"(SYS_GET_PROCESS_INFO), "b"(pid), "c"(info));
  return ret;
}
int sys_get_process_count(void) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_GET_PROCESS_COUNT));
  return ret;
}

int sys_open(const char *path, int flags) {
  int ret;
  asm volatile("int $0x80"
               : "=a"(ret)
               : "a"(SYS_OPEN), "b"(path), "c"(flags));
  return ret;
}

int sys_close(int fd) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_CLOSE), "b"(fd));
  return ret;
}

int sys_lseek(int fd, int offset, int whence) {
  int ret;
  asm volatile("int $0x80"
               : "=a"(ret)
               : "a"(SYS_LSEEK), "b"(fd), "c"(offset), "d"(whence));
  return ret;
}

int sys_stat(const char *path, struct stat *st) {
  int ret;
  asm volatile("int $0x80"
               : "=a"(ret)
               : "a"(SYS_STAT), "b"(path), "c"(st));
  return ret;
}

int sys_fstat(int fd, struct stat *st) {
  int ret;
  asm volatile("int $0x80"
               : "=a"(ret)
               : "a"(SYS_FSTAT), "b"(fd), "c"(st));
  return ret;
}

int sys_mkdir(const char *path) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_MKDIR), "b"(path));
  return ret;
}

int sys_unlink(const char *path) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_UNLINK), "b"(path));
  return ret;
}

int sys_getdents(int fd, struct dirent *buf, int max) {
  int ret;
  asm volatile("int $0x80"
               : "=a"(ret)
               : "a"(SYS_GETDENTS), "b"(fd), "c"(buf), "d"(max));
  return ret;
}

int sys_dup(int fd) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_DUP), "b"(fd));
  return ret;
}

int sys_chdir(const char *path) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_CHDIR), "b"(path));
  return ret;
}

extern char user_c_start;
void user_main() { init_shell(); }
extern char user_c_end;
__asm__(".global user_c_end\nuser_c_end:");