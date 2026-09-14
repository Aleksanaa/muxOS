/*
 * mlibc sysdeps for muxOS.  The kernel syscall numbers match
 * kernel/task/syscall.h and the on-the-wire structs match kernel/fs/fs_uapi.h;
 * both are copied here because this file must not include those headers (they
 * define libc's own struct stat/dirent names).
 *
 * A userspace cwd is kept here and relative paths are made absolute before
 * every filesystem syscall.  There is no kernel cwd yet; with fork stubbed out
 * a per-process libc cwd is equivalent for the single-process applets toybox
 * runs.
 */

#include <abi-bits/errno.h>
#include <bits/syscall.h>
#include <mlibc/all-sysdeps.hpp>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <termios.h>

/* Linux i386 ioctl request numbers (mlibc does not ship sys/ioctl.h). */
#define MUX_TCGETS 0x5401
#define MUX_TCSETS 0x5402
#define MUX_TCSETSW 0x5403
#define MUX_TCSETSF 0x5404
#define MUX_TIOCGPGRP 0x540F
#define MUX_TIOCSPGRP 0x5410
#define MUX_TIOCGWINSZ 0x5413
#define MUX_TIOCGSID 0x5429

#define KSYS_READ 0
#define KSYS_WRITE 1
#define KSYS_EXIT 2
#define KSYS_SLEEP 3
#define KSYS_FORK 4
#define KSYS_EXECVE 5
#define KSYS_WAIT 6
#define KSYS_WAITPID 10
#define KSYS_OPEN 8
#define KSYS_CLOSE 9
#define KSYS_LINK 12
#define KSYS_UNLINK 13
#define KSYS_GETPID 21
#define KSYS_LSEEK 24
#define KSYS_STAT 25
#define KSYS_FSTAT 26
#define KSYS_MKDIR 27
#define KSYS_GETDENTS 28
#define KSYS_DUP 29
#define KSYS_MMAP 30
#define KSYS_MUNMAP 31
#define KSYS_SET_TLS 32
#define KSYS_RENAME 33
#define KSYS_RMDIR 34
#define KSYS_FTRUNCATE 35
#define KSYS_DUP2 36
#define KSYS_CHMOD 37
#define KSYS_FCHMOD 38
#define KSYS_CHDIR 39
#define KSYS_GETCWD 40
#define KSYS_OPENAT 41
#define KSYS_STATAT 42
#define KSYS_UNLINKAT 43
#define KSYS_MKDIRAT 44
#define KSYS_RENAMEAT 45
#define KSYS_LINKAT 46
#define KSYS_PIPE 47
#define KSYS_KILL 48
#define KSYS_SIGACTION 49
#define KSYS_SIGPROCMASK 50
#define KSYS_SIGRETURN 51
#define KSYS_SETPGID 52
#define KSYS_GETPGID 53
#define KSYS_GETSID 54
#define KSYS_SETSID 55
#define KSYS_TCGETPGRP 56
#define KSYS_TCSETPGRP 57
#define KSYS_TTYGETMODE 58
#define KSYS_TTYSETMODE 59

#define MUX_DIRSIZ 32

enum {
	MUX_T_DIR = 1,
	MUX_T_FILE = 2,
	MUX_T_DEVICE = 3,
};

struct muxos_stat {
	uint32_t ino;
	uint16_t type;
	uint16_t mode;
	uint32_t nlink;
	uint32_t size;
	uint32_t dev;
};

struct muxos_dirent {
	uint32_t ino;
	uint32_t off;
	uint32_t type;
	char name[MUX_DIRSIZ];
};

/*
 * There is no real tty line discipline; present a canonical, echoing terminal
 * so programs (toybox, mrsh) that query termios/winsize take their normal
 * interactive paths instead of bailing out.
 */
static void fill_termios(struct termios *t) {
	memset(t, 0, sizeof(*t));
	t->c_iflag = ICRNL | IXON;
	t->c_oflag = OPOST | ONLCR;
	t->c_cflag = CS8 | CREAD | CLOCAL;
	t->c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK;
	t->c_cc[VINTR] = 3;    /* ^C */
	t->c_cc[VQUIT] = 28;   /* ^\ */
	t->c_cc[VERASE] = 127; /* DEL */
	t->c_cc[VKILL] = 21;   /* ^U */
	t->c_cc[VEOF] = 4;     /* ^D */
	t->c_cc[VSTART] = 17;  /* ^Q */
	t->c_cc[VSTOP] = 19;   /* ^S */
	t->c_cc[VSUSP] = 26;   /* ^Z */
	cfsetispeed(t, B38400);
	cfsetospeed(t, B38400);
}

static void fill_winsize(struct winsize *w) {
	w->ws_row = 25; /* kernel console is 80x25 (see terminal.c) */
	w->ws_col = 80;
	w->ws_xpixel = 0;
	w->ws_ypixel = 0;
}

static int term_ioctl(unsigned long request, void *arg) {
	switch (request) {
	case MUX_TCGETS:
		fill_termios((struct termios *)arg);
		return 0;
	case MUX_TCSETS:
	case MUX_TCSETSW:
	case MUX_TCSETSF:
		return 0;
	case MUX_TIOCGWINSZ:
		fill_winsize((struct winsize *)arg);
		return 0;
	case MUX_TIOCGPGRP:
		*(int *)arg = (int)syscall(KSYS_TCGETPGRP);
		return 0;
	case MUX_TIOCSPGRP:
		syscall(KSYS_TCSETPGRP, *(int *)arg);
		return 0;
	case MUX_TIOCGSID:
		*(int *)arg = (int)syscall(KSYS_GETSID, 0);
		return 0;
	default:
		return ENOTTY;
	}
}

/*
 * toybox/other C programs call ioctl() directly; mlibc only defines it under
 * the glibc option, which this port does not build.  Provide it and back it
 * with the same fake terminal as Sysdeps<Ioctl>.
 */
extern "C" int ioctl(int fd, unsigned long request, ...) {
	(void)fd;
	va_list ap;
	va_start(ap, request);
	void *arg = va_arg(ap, void *);
	va_end(ap);

	int e = term_ioctl(request, arg);
	if (e) {
		errno = e;
		return -1;
	}
	return 0;
}

namespace mlibc {

/*
 * Relative paths are resolved by the kernel against its cwd, so this only
 * validates and bounds the string.
 */
static int resolve_path(const char *path, char *out, size_t outsz) {
	size_t n;

	if (!path || !*path)
		return ENOENT;
	n = strlen(path);
	if (n >= outsz)
		return ENAMETOOLONG;
	memcpy(out, path, n + 1);
	return 0;
}

static void fill_stat(struct stat *st, const struct muxos_stat *ks) {
	memset(st, 0, sizeof(*st));
	st->st_dev = ks->dev;
	st->st_ino = ks->ino;
	st->st_nlink = ks->nlink;
	st->st_size = ks->size;
	st->st_blksize = 4096;
	st->st_blocks = (ks->size + 511) / 512;

	mode_t bits = ks->mode & 07777;
	switch (ks->type) {
	case MUX_T_DIR:
		st->st_mode = S_IFDIR | bits;
		break;
	case MUX_T_DEVICE:
		st->st_mode = S_IFCHR | bits;
		break;
	default:
		st->st_mode = S_IFREG | bits;
		break;
	}
}

void Sysdeps<LibcPanic>::operator()() {
	const char *msg = "mlibc: panic\n";
	syscall(KSYS_WRITE, 2, msg, strlen(msg));
	syscall(KSYS_EXIT, 1);
	__builtin_trap();
}

void Sysdeps<LibcLog>::operator()(const char *message) {
	syscall(KSYS_WRITE, 2, message, strlen(message));
}

int Sysdeps<Isatty>::operator()(int fd) {
	/* fds 0-2 are wired to /dev/console at startup. */
	if (fd >= 0 && fd <= 2)
		return 0;
	return ENOTTY;
}

int Sysdeps<Kill>::operator()(pid_t pid, int sig) {
	long r = syscall(KSYS_KILL, pid, sig);
	return r < 0 ? (int)-r : 0;
}

int Sysdeps<GetPgid>::operator()(pid_t pid, pid_t *pgid) {
	long r = syscall(KSYS_GETPGID, pid);
	if (r < 0)
		return (int)-r;
	*pgid = (pid_t)r;
	return 0;
}

int Sysdeps<GetSid>::operator()(pid_t pid, pid_t *sid) {
	long r = syscall(KSYS_GETSID, pid);
	if (r < 0)
		return (int)-r;
	*sid = (pid_t)r;
	return 0;
}

int Sysdeps<SetPgid>::operator()(pid_t pid, pid_t pgid) {
	long r = syscall(KSYS_SETPGID, pid, pgid);
	return r < 0 ? (int)-r : 0;
}

int Sysdeps<SetSid>::operator()(pid_t *sid) {
	long r = syscall(KSYS_SETSID);
	if (r < 0)
		return (int)-r;
	*sid = (pid_t)r;
	return 0;
}

int Sysdeps<Tcgetattr>::operator()(int fd, struct termios *attr) {
	(void)fd;
	fill_termios(attr);
	/* Overlay the line-discipline bits the kernel console actually honours. */
	long r = syscall(KSYS_TTYGETMODE);
	if (r >= 0) {
		int mode = (int)r;
		attr->c_lflag = 0;
		if (mode & 1)
			attr->c_lflag |= ICANON;
		if (mode & 2)
			attr->c_lflag |= ECHO;
		if (mode & 4)
			attr->c_lflag |= ISIG;
	}
	return 0;
}

int Sysdeps<Tcsetattr>::operator()(int fd, int actions,
		const struct termios *attr) {
	(void)fd;
	(void)actions;
	int mode = 0;
	if (attr->c_lflag & ICANON)
		mode |= 1;
	if (attr->c_lflag & ECHO)
		mode |= 2;
	if (attr->c_lflag & ISIG)
		mode |= 4;
	syscall(KSYS_TTYSETMODE, mode);
	return 0;
}

int Sysdeps<Poll>::operator()(struct pollfd *fds, nfds_t count, int timeout,
		int *num_events) {
	(void)timeout;
	int ready = 0;
	for (nfds_t i = 0; i < count; i++) {
		fds[i].revents = 0;
		if (fds[i].fd < 0)
			continue;
		/* Reads block in the kernel and writes never block, so everything
		 * is always ready; blocking is deferred to read()/write(). */
		if (fds[i].events & POLLIN)
			fds[i].revents |= POLLIN;
		if (fds[i].events & POLLOUT)
			fds[i].revents |= POLLOUT;
		if (fds[i].revents)
			ready++;
	}
	*num_events = ready;
	return 0;
}

int Sysdeps<Tcgetwinsize>::operator()(int fd, struct winsize *winsz) {
	(void)fd;
	fill_winsize(winsz);
	return 0;
}

int Sysdeps<Times>::operator()(struct tms *tms, clock_t *out) {
	memset(tms, 0, sizeof(*tms));
	*out = 0;
	return 0;
}

int Sysdeps<Uname>::operator()(struct utsname *buf) {
	memset(buf, 0, sizeof(*buf));
	strcpy(buf->sysname, "muxOS");
	strcpy(buf->nodename, "muxos");
	strcpy(buf->release, "0.0.1");
	strcpy(buf->version, "muxOS");
	strcpy(buf->machine, "i686");
	return 0;
}

pid_t Sysdeps<GetPid>::operator()() {
	long r = syscall(KSYS_GETPID);
	return (pid_t)(r < 0 ? 0 : r);
}

pid_t Sysdeps<GetPpid>::operator()() { return 0; }

uid_t Sysdeps<GetUid>::operator()() { return 0; }
uid_t Sysdeps<GetEuid>::operator()() { return 0; }
gid_t Sysdeps<GetGid>::operator()() { return 0; }
gid_t Sysdeps<GetEgid>::operator()() { return 0; }

int Sysdeps<Fork>::operator()(pid_t *child) {
	long r = syscall(KSYS_FORK);
	if (r < 0)
		return (int)-r;
	*child = (pid_t)r;
	return 0;
}

int Sysdeps<Waitpid>::operator()(pid_t pid, int *status, int flags,
		struct rusage *ru, pid_t *ret_pid) {
	(void)ru;
	for (;;) {
		long r = syscall(KSYS_WAITPID, pid, flags, status);
		if (r == -EAGAIN) {
			/* No child ready yet: yield so it can run. */
			syscall(KSYS_SLEEP, 1);
			continue;
		}
		if (r < 0)
			return (int)-r;
		*ret_pid = (pid_t)r;
		return 0;
	}
}

int Sysdeps<Umask>::operator()(mode_t mode, mode_t *old) {
	(void)mode;
	*old = 0;
	return 0;
}

int Sysdeps<Sleep>::operator()(time_t *secs, long *nanos) {
	/* The timer runs at 1000 Hz, so a tick is a millisecond. */
	long ms = 0;
	if (secs)
		ms += (long)(*secs) * 1000;
	if (nanos)
		ms += *nanos / 1000000;
	if (ms < 1)
		ms = 1;
	syscall(KSYS_SLEEP, ms);
	return 0;
}

int Sysdeps<Sigaction>::operator()(int sig, const struct sigaction *act,
		struct sigaction *old) {
	long r = syscall(KSYS_SIGACTION, sig, act, old);
	return r < 0 ? (int)-r : 0;
}

int Sysdeps<Sigprocmask>::operator()(int how, const sigset_t *set,
		sigset_t *old) {
	long r = syscall(KSYS_SIGPROCMASK, how, set, old);
	return r < 0 ? (int)-r : 0;
}

int Sysdeps<Ioctl>::operator()(int fd, unsigned long request, void *arg,
		int *result) {
	(void)fd;
	if (result)
		*result = 0;
	return term_ioctl(request, arg);
}

int Sysdeps<GetCwd>::operator()(char *buffer, size_t size) {
	long r = syscall(KSYS_GETCWD, buffer, size);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Chdir>::operator()(const char *path) {
	long r = syscall(KSYS_CHDIR, path);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Execve>::operator()(const char *path, char *const argv[],
		char *const envp[]) {
	long r = syscall(KSYS_EXECVE, path, argv, envp);
	return r < 0 ? (int)-r : 0;
}

int Sysdeps<Readlink>::operator()(const char *path, void *buffer, size_t max_size,
		ssize_t *length) {
	(void)buffer;
	(void)max_size;
	(void)length;
	/* No symlinks: EINVAL means "exists but is not a symlink", which is what
	 * realpath()/xabspath() rely on to fall back to stat(). */
	struct muxos_stat ks;
	long r = syscall(KSYS_STAT, path, &ks);
	if (r < 0)
		return (int)-r;
	return EINVAL;
}

int Sysdeps<Readlinkat>::operator()(int dirfd, const char *path, void *buffer,
		size_t max_size, ssize_t *length) {
	(void)buffer;
	(void)max_size;
	(void)length;
	struct muxos_stat ks;
	long r = syscall(KSYS_STATAT, dirfd, path, &ks);
	if (r < 0)
		return (int)-r;
	return EINVAL;
}

int Sysdeps<Access>::operator()(const char *path, int mode) {
	(void)mode;
	char abs[256];
	int e = resolve_path(path, abs, sizeof(abs));
	if (e)
		return e;

	struct muxos_stat ks;
	long r = syscall(KSYS_STAT, abs, &ks);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Faccessat>::operator()(int dirfd, const char *pathname, int mode,
		int flags) {
	(void)mode;
	(void)flags;
	struct muxos_stat ks;
	long r = syscall(KSYS_STATAT, dirfd, pathname, &ks);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Write>::operator()(int fd, const void *buf, size_t count,
		ssize_t *bytes_written) {
	for (;;) {
		long r = syscall(KSYS_WRITE, fd, buf, count);
		if (r == -EAGAIN) {
			/* Pipe is full: yield and retry. */
			syscall(KSYS_SLEEP, 1);
			continue;
		}
		if (r < 0)
			return (int)-r;
		*bytes_written = r;
		return 0;
	}
}

int Sysdeps<Pipe>::operator()(int *fds, int flags) {
	long r = syscall(KSYS_PIPE, fds, flags);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Read>::operator()(int fd, void *buf, size_t count,
		ssize_t *bytes_read) {
	for (;;) {
		long r = syscall(KSYS_READ, fd, buf, count);
		if (r == -EAGAIN) {
			/* Pipe would block: yield and retry. */
			syscall(KSYS_SLEEP, 1);
			continue;
		}
		if (r < 0)
			return (int)-r;
		*bytes_read = r;
		return 0;
	}
}

int Sysdeps<Open>::operator()(const char *pathname, int flags, mode_t mode,
		int *fd) {
	(void)mode;
	char abs[256];
	int e = resolve_path(pathname, abs, sizeof(abs));
	if (e)
		return e;

	int kflags = flags & (O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL |
			O_TRUNC | O_APPEND);
	long r = syscall(KSYS_OPEN, abs, kflags);
	if (r < 0)
		return (int)-r;
	*fd = (int)r;
	return 0;
}

int Sysdeps<Openat>::operator()(int dirfd, const char *pathname, int flags,
		mode_t mode, int *fd) {
	(void)mode;
	int kflags = flags & (O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL |
			O_TRUNC | O_APPEND);
	long r = syscall(KSYS_OPENAT, dirfd, pathname, kflags);
	if (r < 0)
		return (int)-r;
	*fd = (int)r;
	return 0;
}

int Sysdeps<Close>::operator()(int fd) {
	long r = syscall(KSYS_CLOSE, fd);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Seek>::operator()(int fd, off_t offset, int whence,
		off_t *new_offset) {
	long r = syscall(KSYS_LSEEK, fd, offset, whence);
	if (r < 0)
		return ESPIPE; // console/character devices are not seekable
	*new_offset = r;
	return 0;
}

int Sysdeps<Stat>::operator()(fsfd_target fsfdt, int fd, const char *path,
		int flags, struct stat *statbuf) {
	(void)flags;
	struct muxos_stat ks;
	long r;

	if (fsfdt == fsfd_target::fd) {
		r = syscall(KSYS_FSTAT, fd, &ks);
	} else if (fsfdt == fsfd_target::fd_path) {
		if (!path)
			return EFAULT;
		r = syscall(KSYS_STATAT, fd, path, &ks);
	} else {
		if (!path)
			return EFAULT;
		r = syscall(KSYS_STAT, path, &ks);
	}

	if (r < 0)
		return (int)-r;
	fill_stat(statbuf, &ks);
	return 0;
}

int Sysdeps<OpenDir>::operator()(const char *path, int *handle) {
	char abs[256];
	int e = resolve_path(path, abs, sizeof(abs));
	if (e)
		return e;

	struct muxos_stat ks;
	long sr = syscall(KSYS_STAT, abs, &ks);
	if (sr < 0)
		return (int)-sr;
	if (ks.type != MUX_T_DIR)
		return ENOTDIR;

	long r = syscall(KSYS_OPEN, abs, O_RDONLY);
	if (r < 0)
		return (int)-r;
	*handle = (int)r;
	return 0;
}

int Sysdeps<ReadEntries>::operator()(int handle, void *buffer, size_t max_size,
		size_t *bytes_read) {
	char *buf = (char *)buffer;
	size_t written = 0;
	/* Worst case mlibc record, so we never read a dirent we cannot store. */
	const size_t min_rec = offsetof(struct dirent, d_name) + MUX_DIRSIZ;

	while (written + min_rec <= max_size) {
		struct muxos_dirent de;
		long n = syscall(KSYS_GETDENTS, handle, &de, (long)sizeof(de));
		if (n < (long)sizeof(de))
			break;

		size_t namelen = strnlen(de.name, MUX_DIRSIZ);
		size_t reclen = offsetof(struct dirent, d_name) + namelen + 1;
		reclen = (reclen + 3) & ~(size_t)3;
		if (written + reclen > max_size)
			break;

		struct dirent *ent = (struct dirent *)(buf + written);
		memset(ent, 0, offsetof(struct dirent, d_name));
		ent->d_ino = de.ino;
		ent->d_off = (off_t)de.off;
		ent->d_reclen = (reclen_t)reclen;
		switch (de.type) {
		case MUX_T_DIR:
			ent->d_type = DT_DIR;
			break;
		case MUX_T_DEVICE:
			ent->d_type = DT_CHR;
			break;
		case MUX_T_FILE:
			ent->d_type = DT_REG;
			break;
		default:
			ent->d_type = DT_UNKNOWN;
			break;
		}
		memcpy(ent->d_name, de.name, namelen + 1);
		written += reclen;
	}

	*bytes_read = written;
	return 0;
}

int Sysdeps<Mkdir>::operator()(const char *path, mode_t mode) {
	(void)mode;
	char abs[256];
	int e = resolve_path(path, abs, sizeof(abs));
	if (e)
		return e;
	long r = syscall(KSYS_MKDIR, abs);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Mkdirat>::operator()(int dirfd, const char *path, mode_t mode) {
	(void)mode;
	long r = syscall(KSYS_MKDIRAT, dirfd, path);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Rmdir>::operator()(const char *path) {
	char abs[256];
	int e = resolve_path(path, abs, sizeof(abs));
	if (e)
		return e;
	long r = syscall(KSYS_RMDIR, abs);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Unlinkat>::operator()(int dirfd, const char *path, int flags) {
	long r = syscall(KSYS_UNLINKAT, dirfd, path, flags);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Rename>::operator()(const char *path, const char *new_path) {
	char a[256], b[256];
	int e = resolve_path(path, a, sizeof(a));
	if (e)
		return e;
	e = resolve_path(new_path, b, sizeof(b));
	if (e)
		return e;
	long r = syscall(KSYS_RENAME, a, b);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Renameat>::operator()(int olddirfd, const char *old_path,
		int newdirfd, const char *new_path) {
	long r = syscall(KSYS_RENAMEAT, olddirfd, old_path, newdirfd, new_path);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Link>::operator()(const char *old_path, const char *new_path) {
	char a[256], b[256];
	int e = resolve_path(old_path, a, sizeof(a));
	if (e)
		return e;
	e = resolve_path(new_path, b, sizeof(b));
	if (e)
		return e;
	long r = syscall(KSYS_LINK, a, b);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Linkat>::operator()(int olddirfd, const char *old_path,
		int newdirfd, const char *new_path, int flags) {
	(void)flags;
	long r = syscall(KSYS_LINKAT, olddirfd, old_path, newdirfd, new_path);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Truncate>::operator()(const char *path, off_t length) {
	char abs[256];
	int e = resolve_path(path, abs, sizeof(abs));
	if (e)
		return e;

	long fd = syscall(KSYS_OPEN, abs, O_WRONLY);
	if (fd < 0)
		return (int)-fd;
	long r = syscall(KSYS_FTRUNCATE, fd, (long)length);
	syscall(KSYS_CLOSE, fd);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Ftruncate>::operator()(int fd, size_t size) {
	long r = syscall(KSYS_FTRUNCATE, fd, (long)size);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Chmod>::operator()(const char *pathname, mode_t mode) {
	char abs[256];
	int e = resolve_path(pathname, abs, sizeof(abs));
	if (e)
		return e;
	long r = syscall(KSYS_CHMOD, abs, (long)mode);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Fchmod>::operator()(int fd, mode_t mode) {
	long r = syscall(KSYS_FCHMOD, fd, (long)mode);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Fchmodat>::operator()(int dirfd, const char *pathname, mode_t mode,
		int flags) {
	(void)flags;
	if (dirfd != AT_FDCWD)
		return ENOSYS;
	return Sysdeps<Chmod>::operator()(pathname, mode);
}

int Sysdeps<Fchownat>::operator()(int dirfd, const char *pathname, uid_t owner,
		gid_t group, int flags) {
	(void)dirfd;
	(void)pathname;
	(void)owner;
	(void)group;
	(void)flags;
	return 0;
}

int Sysdeps<Utimensat>::operator()(int dirfd, const char *pathname,
		const struct timespec times[2], int flags) {
	(void)dirfd;
	(void)pathname;
	(void)times;
	(void)flags;
	return 0;
}

int Sysdeps<Fsync>::operator()(int fd) {
	(void)fd;
	return 0;
}

void Sysdeps<Sync>::operator()() {}

int Sysdeps<Dup>::operator()(int fd, int flags, int *newfd) {
	(void)flags;
	long r = syscall(KSYS_DUP, fd);
	if (r < 0)
		return (int)-r;
	*newfd = (int)r;
	return 0;
}

int Sysdeps<Dup2>::operator()(int fd, int flags, int newfd) {
	(void)flags;
	long r = syscall(KSYS_DUP2, fd, newfd);
	if (r < 0)
		return (int)-r;
	return 0;
}

int Sysdeps<Fcntl>::operator()(int fd, int request, va_list args, int *result) {
	switch (request) {
	case F_GETFD:
		*result = 0;
		return 0;
	case F_SETFD:
		return 0;
	case F_GETFL:
		*result = 0; /* O_RDONLY */
		return 0;
	case F_SETFL:
		return 0;
	case F_DUPFD: {
		(void)va_arg(args, int);
		long r = syscall(KSYS_DUP, fd);
		if (r < 0)
			return (int)-r;
		*result = (int)r;
		return 0;
	}
	default:
		return EINVAL;
	}
}

int Sysdeps<AnonAllocate>::operator()(size_t size, void **pointer) {
	long r = syscall(KSYS_MMAP, 0, size, 0x3 /* PROT_READ|PROT_WRITE */,
			0x22 /* MAP_PRIVATE|MAP_ANONYMOUS */, -1, 0);
	if (r <= 0)
		return ENOMEM;
	*pointer = (void *)r;
	return 0;
}

int Sysdeps<AnonFree>::operator()(void *pointer, size_t size) {
	(void)pointer;
	long r = syscall(KSYS_MUNMAP, pointer, size);
	if (r < 0)
		return EINVAL;
	return 0;
}

int Sysdeps<VmMap>::operator()(void *hint, size_t size, int prot, int flags,
		int fd, off_t offset, void **window) {
	(void)prot;
	(void)flags;
	(void)fd;
	(void)offset;
	long r = syscall(KSYS_MMAP, hint, size, 0x3, 0x22, -1, 0);
	if (r <= 0)
		return ENOMEM;
	*window = (void *)r;
	return 0;
}

int Sysdeps<VmUnmap>::operator()(void *pointer, size_t size) {
	long r = syscall(KSYS_MUNMAP, pointer, size);
	if (r < 0)
		return EINVAL;
	return 0;
}

int Sysdeps<TcbSet>::operator()(void *pointer) {
	long r = syscall(KSYS_SET_TLS, pointer);
	if (r < 0)
		return EIO;
	return 0;
}

int Sysdeps<ClockGet>::operator()(int clock, time_t *secs, long *nanos) {
	(void)clock;
	*secs = 0;
	*nanos = 0;
	return 0;
}

int Sysdeps<FutexWait>::operator()(int *pointer, int expected,
		const struct timespec *time) {
	(void)pointer;
	(void)expected;
	(void)time;
	return 0;
}

int Sysdeps<FutexWake>::operator()(int *pointer, bool all) {
	(void)pointer;
	(void)all;
	return 0;
}

void Sysdeps<Exit>::operator()(int status) {
	syscall(KSYS_EXIT, status);
	__builtin_unreachable();
}

} // namespace mlibc
