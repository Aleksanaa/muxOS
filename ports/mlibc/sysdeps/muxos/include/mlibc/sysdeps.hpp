#pragma once

#include <mlibc/sysdep-signatures.hpp>

namespace mlibc {

struct MuxosSysdepTags :
	LibcPanic,
	LibcLog,
	Isatty,
	Write,
	TcbSet,
	AnonAllocate,
	AnonFree,
	Seek,
	Exit,
	Execve,
	Close,
	FutexWake,
	FutexWait,
	Read,
	Open,
	Openat,
	VmMap,
	VmUnmap,
	ClockGet,
	GetPid,
	GetPpid,
	Fork,
	Waitpid,
	Umask,
	Sigaction,
	Sigprocmask,
	Ioctl,
	GetCwd,
	Chdir,
	Readlink,
	Readlinkat,
	Access,
	Faccessat,
	Stat,
	OpenDir,
	ReadEntries,
	Mkdir,
	Mkdirat,
	Rmdir,
	Unlinkat,
	Rename,
	Renameat,
	Link,
	Linkat,
	Truncate,
	Ftruncate,
	Chmod,
	Fchmod,
	Fchmodat,
	Fchownat,
	Utimensat,
	Fsync,
	Sync,
	Dup,
	Dup2,
	Fcntl
{};

template<typename Tag>
using Sysdeps = SysdepOf<MuxosSysdepTags, Tag>;

struct SysdepTraits {
	static constexpr bool usesRtNetlink = false;
};

} // namespace mlibc
