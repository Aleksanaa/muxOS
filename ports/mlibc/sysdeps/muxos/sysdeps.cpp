/*
 * mlibc sysdeps for muxOS.  The kernel syscall numbers match
 * kernel/task/syscall.h; only the sysdeps required for a static "hello world"
 * are implemented, everything else is absent (mlibc then returns ENOSYS).
 */

#include <abi-bits/errno.h>
#include <bits/syscall.h>
#include <mlibc/all-sysdeps.hpp>
#include <string.h>

#define KSYS_READ 0
#define KSYS_WRITE 1
#define KSYS_EXIT 2
#define KSYS_OPEN 8
#define KSYS_CLOSE 9
#define KSYS_LSEEK 24
#define KSYS_MMAP 30
#define KSYS_MUNMAP 31
#define KSYS_SET_TLS 32

namespace mlibc {

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
	(void)fd;
	/* Report a tty so stdio behaves; we do not model ttys yet. */
	return 0;
}

int Sysdeps<Write>::operator()(int fd, const void *buf, size_t count,
		ssize_t *bytes_written) {
	long r = syscall(KSYS_WRITE, fd, buf, count);
	if (r < 0)
		return EIO;
	*bytes_written = r;
	return 0;
}

int Sysdeps<Read>::operator()(int fd, void *buf, size_t count,
		ssize_t *bytes_read) {
	long r = syscall(KSYS_READ, fd, buf, count);
	if (r < 0)
		return EIO;
	*bytes_read = r;
	return 0;
}

int Sysdeps<Open>::operator()(const char *pathname, int flags, mode_t mode,
		int *fd) {
	long r = syscall(KSYS_OPEN, pathname, flags, mode);
	if (r < 0)
		return ENOENT;
	*fd = (int)r;
	return 0;
}

int Sysdeps<Close>::operator()(int fd) {
	long r = syscall(KSYS_CLOSE, fd);
	if (r < 0)
		return EIO;
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
