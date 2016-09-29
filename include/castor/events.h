
#ifndef __RREVENT_H__
#define __RREVENT_H__

#define RREVENT_TABLE\
    RREVENT(EXIT,		0x55AA55AA)\
    RREVENT(DATA,		0x88888888)\
    RREVENT(LOCKED_EVENT,	0x77777777)\
    RREVENT(MUTEX_INIT,		0x00010001)\
    RREVENT(MUTEX_DESTROY,	0x00010002)\
    RREVENT(MUTEX_LOCK,		0x00010003)\
    RREVENT(MUTEX_TRYLOCK,	0x00010004)\
    RREVENT(MUTEX_UNLOCK,	0x00010005)\
    RREVENT(THREAD_CREATE,	0x00010008)\
    RREVENT(FORK,		0x00010009)\
    RREVENT(OPEN,		0x00020001)\
    RREVENT(OPENAT,		0x00020002)\
    RREVENT(CLOSE,		0x00020003)\
    RREVENT(READ,		0x00020004)\
    RREVENT(WRITE,		0x00020005)\
    RREVENT(IOCTL,		0x00020006)\
    RREVENT(FSTAT,		0x00020007)\
    RREVENT(FSTATAT,		0x00020008)\
    RREVENT(MMAPFD,		0x00020009)\
    RREVENT(LINK,		0x0002000A)\
    RREVENT(SYMLINK,		0x0002000B)\
    RREVENT(UNLINK,		0x0002000C)\
    RREVENT(RENAME,		0x0002000D)\
    RREVENT(MKDIR,		0x0002000E)\
    RREVENT(RMDIR,		0x0002000F)\
    RREVENT(CHDIR,		0x00020010)\
    RREVENT(CHMOD,		0x00020011)\
    RREVENT(ACCESS,		0x00020012)\
    RREVENT(TRUNCATE,		0x00020013)\
    RREVENT(FTRUNCATE,		0x00020014)\
    RREVENT(FLOCK,		0x00020015)\
    RREVENT(FSYNC,		0x00020016)\
    RREVENT(LSEEK,		0x00020017)\
    RREVENT(LSTAT,		0x00020018)\
    RREVENT(UMASK,		0x00020019)\
    RREVENT(FCNTL,		0x0002001A)\
    RREVENT(SOCKET,		0x00030001)\
    RREVENT(BIND,		0x00030002)\
    RREVENT(LISTEN,		0x00030003)\
    RREVENT(ACCEPT,		0x00030004)\
    RREVENT(CONNECT,		0x00030005)\
    RREVENT(POLL,		0x00030006)\
    RREVENT(KQUEUE,		0x00030007)\
    RREVENT(KEVENT,		0x00030008)\
    RREVENT(SETSOCKOPT,		0x00030009)\
    RREVENT(GETSOCKOPT,		0x0003000A)\
    RREVENT(GETPEERNAME,	0x0003000B)\
    RREVENT(GETSOCKNAME,	0x0003000C)\
    RREVENT(GETTIME,		0x00040001)\
    RREVENT(SYSCTL,		0x00040002)\
    RREVENT(SETUID,		0x00050001)\
    RREVENT(SETEUID,		0x00050002)\
    RREVENT(SETGID,		0x00050003)\
    RREVENT(SETEGID,		0x00050004)\
    RREVENT(GETUID,		0x00050005)\
    RREVENT(GETEUID,		0x00050006)\
    RREVENT(GETGID,		0x00050007)\
    RREVENT(GETEGID,		0x00050008)\
    RREVENT(SETGROUPS,		0x00050009)\
    RREVENT(GETGROUPS,		0x0005000A)\
    RREVENT(GETRLIMIT,		0x00060001)\
    RREVENT(SETRLIMIT,		0x00060002)\
    RREVENT(GETRUSAGE,		0x00060003)\
    RREVENT(RDTSC,		0x00070001)\
    RREVENT(ATOMIC_LOAD,	0x00070002)\
    RREVENT(ATOMIC_STORE,	0x00070003)\
    RREVENT(ATOMIC_RMW,		0x00070004)\
    RREVENT(ATOMIC_XCHG,	0x00070005)\
    RREVENT(INLINE_ASM,		0x00070006)

#define RREVENT(_a, _b) RREVENT_##_a = _b,
enum {
    RREVENT_TABLE
};
#undef RREVENT

#endif

