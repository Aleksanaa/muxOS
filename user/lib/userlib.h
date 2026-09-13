#ifndef USERLIB
#define USERLIB
#include "fs_uapi.h"
#include "process.h"
#include <stdint.h>

/* process / console syscalls */
int sys_write(int fd, const char *buf, int len);
void sys_exit();
void sys_sleep(int ticks);
int sys_fork();
int sys_execve(const char *path, char **argv);
int sys_wait();
int sys_read(int fd, void *buf, unsigned int count);
int sys_restart(void);
int sys_shutdown(void);
int sys_clear(void);
int sys_getchar(void);
int sys_vgaspace(void);
int sys_getpid(void);
int sys_get_process_info(uint32_t pid, process_info_t *info);
int sys_get_process_count(void);

/* filesystem syscalls */
int sys_open(const char *path, int flags);
int sys_close(int fd);
int sys_lseek(int fd, int offset, int whence);
int sys_stat(const char *path, struct stat *st);
int sys_fstat(int fd, struct stat *st);
int sys_mkdir(const char *path);
int sys_unlink(const char *path);
int sys_getdents(int fd, struct dirent *buf, int max);
int sys_dup(int fd);
int sys_chdir(const char *path);

/* string */
int strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, int n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, int n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, int n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);

/* memory */
void *memset(void *dest, int c, unsigned int n);
void *memcpy(void *dest, const void *src, unsigned int n);
void *memmove(void *dest, const void *src, unsigned int n);
int memcmp(const void *a, const void *b, unsigned int n);

/* ctype */
int isdigit(int c);
int isalpha(int c);
int isalnum(int c);
int islower(int c);
int isupper(int c);
int isspace(int c);
int isprint(int c);
int isxdigit(int c);
int tolower(int c);
int toupper(int c);

void itoa(int value, char *buf);
#endif
