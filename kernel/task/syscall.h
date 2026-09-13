#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#define STDIN 0
#define STDOUT 1
#define STDERR 2

#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_EXIT 2
#define SYS_SLEEP 3
#define SYS_FORK 4
#define SYS_EXECVE 5
#define SYS_WAIT 6
// TODO
#define SYS_RESTART_SYSCALL 7
#define SYS_OPEN 8
#define SYS_CLOSE 9
#define SYS_WAITPID 10
#define SYS_CREAT 11
#define SYS_LINK 12
#define SYS_UNLINK 13
#define SYS_SETUID 14
#define SYS_GETUID 15
#define SYS_SHUTDOWN 16
#define SYS_POWEROFF 17
#define SYS_CLEAR 18
#define SYS_GETCHAR 19
#define SYS_VGASPACE 20
#define SYS_GETPID 21
#define SYS_GET_PROCESS_INFO 22
#define SYS_GET_PROCESS_COUNT 23
#define SYS_LSEEK 24
#define SYS_STAT 25
#define SYS_FSTAT 26
#define SYS_MKDIR 27
#define SYS_GETDENTS 28
#define SYS_DUP 29
#define SYS_MMAP 30
#define SYS_MUNMAP 31
#define SYS_SET_TLS 32
#define SYS_RENAME 33
#define SYS_RMDIR 34
#define SYS_FTRUNCATE 35
#define SYS_DUP2 36
#define SYS_CHMOD 37
#define SYS_FCHMOD 38
#define SYS_CHDIR 39
#define SYS_GETCWD 40
#define SYS_OPENAT 41
#define SYS_STATAT 42
#define SYS_UNLINKAT 43
#define SYS_MKDIRAT 44
#define SYS_RENAMEAT 45
#define SYS_LINKAT 46
#define SYS_PIPE 47
#define SYS_KILL 48
#define SYS_SIGACTION 49
#define SYS_SIGPROCMASK 50
#define SYS_SIGRETURN 51
#define SYS_SETPGID 52
#define SYS_GETPGID 53
#define SYS_GETSID 54
#define SYS_SETSID 55
#define SYS_TCGETPGRP 56
#define SYS_TCSETPGRP 57
void syscall_init();
int syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx,
                    uint32_t esi, uint32_t edi, uint32_t ebp);

#endif
