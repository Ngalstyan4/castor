#include "events_gen.h"

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
    RREVENT(THREAD_CREATE,	0x00010006)\
    RREVENT(FORK,		0x00010007)\
    RREVENT(FORKEND,		0x00010008)\
    RREVENT(GETPID,		0x00010009)\
    RREVENT(WAIT,		0x0001000A)\
    RREVENT(GETPPID,		0x0001000B)\
    RREVENT(COND_INIT,		0x0001000C)\
    RREVENT(COND_DESTROY,	0x0001000D)\
    RREVENT(COND_WAIT,		0x0001000E)\
    RREVENT(COND_SIGNAL,	0x0001000F)\
    RREVENT(COND_BROADCAST,	0x00010010)\
    RREVENT(THREAD_ONCE,	0x00010011)\
    RREVENT(OPEN,		0x00020001)\
    RREVENT(OPENAT,		0x00020002)\
    RREVENT(CLOSE,		0x00020003)\
    RREVENT(READ,		0x00020004)\
    RREVENT(WRITE,		0x00020005)\
    RREVENT(IOCTL,		0x00020006)\
    RREVENT(MMAPFD,		0x00020009)\
    RREVENT(FLOCK,		0x00020015)\
    RREVENT(UMASK,		0x00020019)\
    RREVENT(FCNTL,		0x0002001A)\
    RREVENT(GETDIRENTRIES,	0x0002001D)\
    RREVENT(GETDENTS,		0x0002001E)\
    RREVENT(PREAD,		0x00020020)\
    RREVENT(DUP,		0x00020024)\
    RREVENT(DUP2,		0x00020025)\
    RREVENT(PWRITE,		0x00020026)\
    RREVENT(GETCWD,		0x00020027)\
    RREVENT(READV,              0x00020028)\
    RREVENT(WRITEV,             0x00020029)\
    RREVENT(ACCEPT,		0x00030004)\
    RREVENT(ACCEPT4,		0x00030005)\
    RREVENT(KQUEUE,		0x00030007)\
    RREVENT(KEVENT,		0x00030008)\
    RREVENT(GETSOCKOPT,		0x0003000A)\
    RREVENT(GETPEERNAME,	0x0003000B)\
    RREVENT(SELECT,		0x0003000F)\
    RREVENT(RECVMSG,		0x00030010)\
    RREVENT(RECVFROM,		0x00030011)\
    RREVENT(PIPE,		0x00030012)\
    RREVENT(PIPE2,		0x00030013)\
    RREVENT(SENDFILE,		0x00030014)\
    RREVENT(POLL,		0x00030015)\
    RREVENT(GETTIME,		0x00040001)\
    RREVENT(SYSCTL,		0x00040002)\
    RREVENT(GETTIMEOFDAY,	0x00040003)\
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
    RREVENT(RDTSC,		0x00070001)\
    RREVENT(ATOMIC_LOAD,	0x00070002)\
    RREVENT(ATOMIC_STORE,	0x00070003)\
    RREVENT(ATOMIC_RMW,		0x00070004)\
    RREVENT(ATOMIC_XCHG,	0x00070005)\
    RREVENT(INLINE_ASM,		0x00070006)\
    RREVENT(CAP_ENTER,		0x00080000)\
    RREVENT(CAP_RIGHTS_LIMIT,	0x00080001)\
    RREVENT(SHMGET,		0x00090000)\
    RREVENT(SEMGET,		0x00090001)\
    RREVENT(SEMCTL,		0x00090002)\
    RREVENT(SHM_OPEN,		0x00090080)\
    RREVENT(SHM_UNLINK,		0x00090081)\
    RREVENT(SEM_OPEN,		0x00090082)\
    RREVENT(SEM_CLOSE,		0x00090083)\
    RREVENT(SEM_UNLINK,		0x00090084)\
    RREVENT(SEM_POST,		0x00090085)\
    RREVENT(SEM_WAIT,		0x00090086)\
    RREVENT(SEM_TRYWAIT,	0x00090087)\
    RREVENT(SEM_TIMEDWAIT,	0x00090088)\
    RREVENT(SEM_GETVALUE,	0x00090089)
    /*   0x000A000 range reserved for autogenerated code*/

#define RREVENT(_a, _b) RREVENT_##_a = _b,
enum {
    RREVENT_TABLE
    RREVENT_TABLE_GEN
};
#undef RREVENT

#endif

