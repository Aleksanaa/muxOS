#ifndef USERLIB
#define USERLIB
#include "process.h"
#include <stdint.h>
int sys_write(int fd, const char *buf, int len);
void sys_exit();
void sys_sleep(int ticks);
int sys_fork();
int sys_exec(uint32_t start, uint32_t size);
int sys_wait();
int sys_read(int fd, void *buf, unsigned int count);
int sys_restart(void);
int sys_shutdown(void);
int sys_read(int fd, void *buf, unsigned int count);
int sys_restart(void);
int sys_clear(void);
int sys_getchar(void);
int sys_vgaspace(void);
int sys_getpid(void);
int sys_get_process_info(uint32_t pid, process_info_t *info);
int sys_get_process_count(void);
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