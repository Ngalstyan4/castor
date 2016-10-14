
#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <threads.h>

#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>

#include <sys/cdefs.h>
#include <sys/syscall.h>

// stat/fstat
#include <sys/stat.h>

//getrlimit, setrlimit
#include <sys/resource.h>

// sysctl
#include <sys/sysctl.h>

// socket
#include <sys/socket.h>

// ioctl
#include <sys/ioccom.h>

// poll/kqueue
#include <poll.h>
#include <sys/event.h>
#include <sys/time.h>

// mmap
#include <sys/mman.h>

//getdirentries
#include <sys/types.h>
#include <dirent.h>

//statfs/fstatfs
#include <sys/param.h>
#include <sys/mount.h>

#include <libc_private.h>

#include <castor/debug.h>
#include <castor/rrlog.h>
#include <castor/rrplay.h>
#include <castor/rrgq.h>
#include <castor/mtx.h>
#include <castor/events.h>

#include "util.h"

/* USE FILES */
bool useRealFiles = false;

extern int
_pthread_create(pthread_t * thread, const pthread_attr_t * attr,
	       void *(*start_routine) (void *), void *arg);
extern int
_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
extern int _pthread_mutex_trylock(pthread_mutex_t *mutex);
extern int __pthread_mutex_lock(pthread_mutex_t *mutex);
extern int _pthread_mutex_unlock(pthread_mutex_t *mutex);
extern int _pthread_mutex_destroy(pthread_mutex_t *mutex);
extern int __vdso_clock_gettime(clockid_t clock_id, struct timespec *tp);
extern void *__sys_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
extern interpos_func_t __libc_interposing[] __hidden;

extern void LogDone();

typedef struct ThreadState {
    void	*(*start)(void *);
    void	*arg;
} ThreadState;

static ThreadState threadState[RRLOG_MAX_THREADS];

void *
thrwrapper(void *arg)
{
    uintptr_t thrNo = (uintptr_t)arg;

    threadId = thrNo;

    return (threadState[thrNo].start)(threadState[thrNo].arg);
}

/*
 * pthread_create can cause log entries to be out of order because of thread 
 * creation.  We should add the go code that prevents log entries in the middle 
 * of the current log entry.
 */
int
pthread_create(pthread_t * thread, const pthread_attr_t * attr,
	       void *(*start_routine) (void *), void *arg)
{
    int result;
    uintptr_t thrNo;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return _pthread_create(thread, attr, start_routine, arg);
    }

    if (rrMode == RRMODE_RECORD) {
	thrNo = RRShared_AllocThread(rrlog);

	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_THREAD_CREATE;
	//e->value[0] = result;
	e->value[1] = thrNo;
	RRLog_Append(rrlog, e);


	threadState[thrNo].start = start_routine;
	threadState[thrNo].arg = arg;

	result = _pthread_create(thread, attr, thrwrapper, (void *)thrNo);
    } else {
	int savedResult;

	e = RRPlay_Dequeue(rrlog, threadId);
	thrNo = e->value[1];
	savedResult = e->value[0];
	AssertEvent(e, RREVENT_THREAD_CREATE);
	RRPlay_Free(rrlog, e);

	RRShared_SetupThread(rrlog, thrNo);

	threadState[thrNo].start = start_routine;
	threadState[thrNo].arg = arg;

	result = _pthread_create(thread, attr, thrwrapper, (void *)thrNo);

	assert(result == savedResult);
    }

    return result;
}

pid_t
__rr_fork(void)
{
    pid_t result;
    uintptr_t thrNo;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return fork();
    }

    if (rrMode == RRMODE_RECORD) {
	thrNo = RRShared_AllocThread(rrlog);

	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_FORK;
	e->value[1] = thrNo;
	RRLog_Append(rrlog, e);

	result = __sys_fork();

	if (result == 0) {
	    threadId = thrNo;
	} else if (result < 0) {
	    perror("FIXME: fork failed during record");
	    abort();
	}
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	thrNo = e->value[1];
	AssertEvent(e, RREVENT_FORK);
	RRPlay_Free(rrlog, e);

	result = __sys_fork();

	if (result == 0) {
	    threadId = thrNo;
	} else if (result < 0) {
	    perror("FIXME: fork failed during replay");
	    abort();
	}
    }

    return result;
}

int
pthread_mutex_init(pthread_mutex_t *mtx, const pthread_mutexattr_t *attr)
{
    RRLogEntry *e;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    break;
	case RRMODE_RECORD:
	    e = RRLog_Alloc(rrlog, threadId);
	    e->event = RREVENT_MUTEX_INIT;
	    e->objectId = (uint64_t)mtx;
	    RRLog_Append(rrlog, e);
	    break;
	case RRMODE_REPLAY:
	    e = RRPlay_Dequeue(rrlog, threadId);
	    AssertEvent(e, RREVENT_MUTEX_INIT);
	    RRPlay_Free(rrlog, e);
	    break;
    }
    return _pthread_mutex_init(mtx, attr);
}

int
pthread_mutex_destroy(pthread_mutex_t *mtx)
{
    RRLogEntry *e;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    break;
	case RRMODE_RECORD:
	    e = RRLog_Alloc(rrlog, threadId);
	    e->event = RREVENT_MUTEX_DESTROY;
	    e->objectId = (uint64_t)mtx;
	    RRLog_Append(rrlog, e);
	    break;
	case RRMODE_REPLAY:
	    e = RRPlay_Dequeue(rrlog, threadId);
	    AssertEvent(e, RREVENT_MUTEX_DESTROY);
	    RRPlay_Free(rrlog, e);
	    break;
    }

    return _pthread_mutex_destroy(mtx);
}

int
_pthread_mutex_lock(pthread_mutex_t *mtx)
{
    int result = 0;
    RRLogEntry *e;

    switch (rrMode) {
	case RRMODE_NORMAL: {
	    result = __pthread_mutex_lock(mtx);
	    break;
	}
	case RRMODE_RECORD: {
	    RRLog_LEnter(threadId, (uint64_t)mtx);

	    result = __pthread_mutex_lock(mtx);

	    e = RRLog_LAlloc(threadId);
	    e->event = RREVENT_MUTEX_LOCK;
	    e->objectId = (uint64_t)mtx;
	    e->value[0] = result;
	    RRLog_LAppend(e);
	    break;
	}
	case RRMODE_REPLAY: {
	    RRPlay_LEnter(threadId, (uint64_t)mtx);

	    result = __pthread_mutex_lock(mtx);

	    e = RRPlay_LDequeue(threadId);
	    AssertEvent(e, RREVENT_MUTEX_LOCK);
	    // ASSERT result = e->value[0];
	    RRPlay_LFree(e);
	    break;
	}
	/*
	 * case RRMODE_FDREPLAY: {
	 *     e = RRLog_Alloc(rrlog, threadId);
	 *     e->objectId = 1;
	 *     result = _pthread_mutex_lock(mtx);
	 *     RRLog_Append(rrlog, e);
	 *     break;
	 * }
	 * case RRMODE_FTREPLAY: {
	 *     e = RRPlay_Dequeue(rrlog, threadId);
	 *     result = _pthread_mutex_lock(mtx);
	 *     RRPlay_Free(rrlog, e);
	 *     break;
	 * }
	 */
    }

    return result;
}

int
pthread_mutex_trylock(pthread_mutex_t *mtx)
{
    int result = 0;
    RRLogEntry *e;

    switch (rrMode) {
	case RRMODE_NORMAL: {
	    result = _pthread_mutex_trylock(mtx);
	    break;
	}
	case RRMODE_RECORD: {
	    e = RRLog_Alloc(rrlog, threadId);
	    e->event = RREVENT_MUTEX_TRYLOCK;
	    e->objectId = (uint64_t)mtx;
	    result = _pthread_mutex_trylock(mtx);
	    e->value[0] = result;
	    RRLog_Append(rrlog, e);
	    break;
	}
	case RRMODE_REPLAY: {
	    e = RRPlay_Dequeue(rrlog, threadId);
	    result = e->value[0];
	    RRPlay_Free(rrlog, e);
	    break;
	}
	/*
	 * case RRMODE_FDREPLAY: {
	 *     e = RRLog_Alloc(rrlog, threadId);
	 *     e->objectId = 1;
	 *     result = _pthread_mutex_trylock(mtx);
	 *     RRLog_Append(rrlog, e);
	 *     break;
	 * }
	 * case RRMODE_FTREPLAY: {
	 *     e = RRPlay_Dequeue(rrlog, threadId);
	 *     result = _pthread_mutex_trylock(mtx);
	 *     RRPlay_Free(rrlog, e);
	 *     break;
	 * }
	 */
    }

    return result;
}

int
pthread_mutex_unlock(pthread_mutex_t *mtx)
{
    int result = 0;
    RRLogEntry *e;

    switch (rrMode) {
	case RRMODE_NORMAL: {
	    result = _pthread_mutex_unlock(mtx);
	    break;
	}
	case RRMODE_RECORD: {
	    result = _pthread_mutex_unlock(mtx);
	    e = RRLog_Alloc(rrlog, threadId);
	    e->event = RREVENT_MUTEX_UNLOCK;
	    e->objectId = (uint64_t)mtx;
	    e->value[0] = result;
	    RRLog_Append(rrlog, e);
	    break;
	}
	case RRMODE_REPLAY: {
	    result = _pthread_mutex_unlock(mtx);
	    e = RRPlay_Dequeue(rrlog, threadId);
	    AssertEvent(e, RREVENT_MUTEX_UNLOCK);
	    // ASSERT result = e->value[0];
	    RRPlay_Free(rrlog, e);
	    break;
	}
	/*
	 * case RRMODE_FDREPLAY: {
	 *     result = _pthread_mutex_unlock(mtx);
	 *     break;
	 * }
	 * case RRMODE_FTREPLAY: {
	 *     result = _pthread_mutex_unlock(mtx);
	 *     break;
	 * }
	 */
    }

    return result;
}

//XXX: convert
int
__sys_open(const char *path, int flags, ...)
{
    int result;
    int mode;
    va_list ap;
    RRLogEntry *e;

    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_open, path, flags, mode);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_open, path, flags, mode);

	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_OPEN;
	e->objectId = 0;
	e->value[0] = result;
	RRLog_Append(rrlog, e);
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_OPEN);
	result = e->value[0];
	RRPlay_Free(rrlog, e);
    }

    return result;
}

//XXX: convert
int
__rr_openat(int fd, const char *path, int flags, ...)
{
    int result;
    mode_t mode;
    va_list ap;
    RRLogEntry *e;

    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_openat, fd, path, flags, mode);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_openat, fd, path, flags, mode);

	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_OPENAT;
	e->objectId = 0;
	e->value[0] = result;
	RRLog_Append(rrlog, e);
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_OPENAT);
	result = e->value[0];
	RRPlay_Free(rrlog, e);
    }

    return result;
}

//XXX: convert
int
__sys_close(int fd)
{
    int result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_close, fd);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_close, fd);

	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_CLOSE;
	e->objectId = fd;
	e->value[0] = result;
	RRLog_Append(rrlog, e);
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_CLOSE);
	result = e->value[0];
	RRPlay_Free(rrlog, e);
    }

    return result;
}

//XXX: convert
ssize_t
__sys_read(int fd, void *buf, size_t nbytes)
{
    ssize_t result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_read, fd, buf, nbytes);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_read, fd, buf, nbytes);

	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_READ;
	e->value[0] = result;
	RRLog_Append(rrlog, e);

	if (result > 0) {
	    logData(buf, result);
	}
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	result = e->value[0];
	AssertEvent(e, RREVENT_READ);
	RRPlay_Free(rrlog, e);

	if (result > 0) {
	    logData(buf, result);
	}
    }

    return result;
}

//XXX: remove eventually
ssize_t
_read(int fd, void *buf, size_t nbytes)
{
    return read(fd, buf, nbytes);
}


//XXX: convert
ssize_t
__sys_write(int fd, const void *buf, size_t nbytes)
{
    int result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_write, fd, buf, nbytes);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_write, fd, buf, nbytes);

	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_WRITE;
	e->objectId = fd;
	e->value[0] = result;
	e->value[1] = hashData((uint8_t *)buf, nbytes);
	RRLog_Append(rrlog, e);
    } else {
	/*
	 * We should only write the same number of bytes as we did during 
	 * recording.  The output divergence test is more conservative than it 
	 * needs to be.  We can assert only on bytes actually written out.
	 */
	if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
	    // Print console output
	    syscall(SYS_write, fd, buf, nbytes);
	}

	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_WRITE);
	AssertOutput(e, e->value[1], (uint8_t *)buf, nbytes);
	result = e->value[0];
	RRPlay_Free(rrlog, e);
    }

    return result;
}

//XXX: remove eventually
ssize_t
_write(int fd, const void *buf, size_t nbytes)
{
    return write(fd, buf, nbytes);
}

//XXX: convert
int
__sys_ioctl(int fd, unsigned long request, ...)
{
    int result;
    va_list ap;
    char *argp;
    bool output = ((IOC_OUT & request) == IOC_OUT);
    unsigned long datalen = IOCPARM_LEN(request);
    RRLogEntry *e;

    va_start(ap, request);
    argp = va_arg(ap, char *);
    va_end(ap);

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_ioctl, fd, request, argp);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_ioctl, fd, request, argp);

	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_IOCTL;
	e->objectId = fd;
	e->value[0] = result;
	e->value[1] = request;
	RRLog_Append(rrlog, e);

	if ((result == 0) && output) {
	    logData((uint8_t *)argp, datalen);
	}
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_IOCTL);
	result = e->value[0];
	RRPlay_Free(rrlog, e);

	if ((result == 0) && output) {
	    logData((uint8_t *)argp, datalen);
	}
    }

    return result;
}

//XXX: convert
int
__rr_fcntl(int fd, int cmd, ...)
{
    ssize_t result;
    RRLogEntry *e;
    va_list ap;
    int arg;

    va_start(ap, cmd);
    arg = va_arg(ap, int);
    va_end(ap);

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_fcntl, fd, cmd, arg);
    }
    NOT_IMPLEMENTED((cmd == F_GETFL) || (cmd == F_SETFL));
    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_fcntl, fd, cmd, arg);
	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_FCNTL;
	e->objectId = fd;
	e->value[0] = result;
	e->value[1] = arg;
	RRLog_Append(rrlog, e);
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_FCNTL);
	result = e->value[0];
	RRPlay_Free(rrlog, e);
    }

    return result;
}

//XXX: convert
int
__rr_getdirentries(int fd, char *buf, int nbytes, long *basep)
{
    int result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_getdirentries, fd, buf, nbytes, basep);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_getdirentries, fd, buf, nbytes, basep);
	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_GETDIRENTRIES;
	e->objectId = fd;
	e->value[0] = result;
	e->value[2] = *basep;
	RRLog_Append(rrlog, e);

	if (result >= 0) {
	    logData((uint8_t*)buf, result);
	}
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_GETDIRENTRIES);
	result = e->value[0];
	*basep = e->value[2];
	RRPlay_Free(rrlog, e);

	if (result >= 0) {
	    logData((uint8_t*)buf, result);
	}
    }

    return result;
}

//XXX: convert
ssize_t
__rr_readlink(const char *restrict path, char *restrict buf, size_t bufsize)
{
    ssize_t result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_readlink, path, buf, bufsize);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_readlink, path, buf, bufsize);

	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_READLINK;
	e->value[0] = result;
	RRLog_Append(rrlog, e);

	if (result > 0) {
	    logData((uint8_t*)buf, bufsize);
	}
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	result = e->value[0];
	AssertEvent(e, RREVENT_READLINK);
	RRPlay_Free(rrlog, e);

	if (result > 0) {
	    logData((uint8_t*)buf, bufsize);
	}
    }
    return result;
}

//XXX: convert
int
__clock_gettime(clockid_t clock_id, struct timespec *tp)
{
    int result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return __vdso_clock_gettime(clock_id, tp);
    }

    if (rrMode == RRMODE_RECORD) {
	result = __vdso_clock_gettime(clock_id, tp);

	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_GETTIME;
	e->objectId = clock_id;
	e->value[0] = tp->tv_sec;
	e->value[1] = tp->tv_nsec;
	e->value[2] = result;
	RRLog_Append(rrlog, e);
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_GETTIME);
	tp->tv_sec = e->value[0];
	tp->tv_nsec = e->value[1];
	result = e->value[2];
	RRPlay_Free(rrlog, e);
    }

    return result;
}

//XXX: convert
int __rr_sysctl(const int *name, u_int namelen, void *oldp,
	     size_t *oldlenp, const void *newp, size_t newlen)
{
    ssize_t result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS___sysctl, name, namelen, oldp, oldlenp, newp, newlen);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS___sysctl, name, namelen, oldp, oldlenp, newp, newlen);

	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_SYSCTL;
	e->value[0] = result;
	if (oldp) {
		e->value[1] = *oldlenp;
	}
	RRLog_Append(rrlog, e);

	if (oldp) {
	    logData(oldp, *oldlenp);
	}
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	result = e->value[0];
	if (oldp) {
	    *oldlenp = e->value[1];
	}
	AssertEvent(e, RREVENT_SYSCTL);
	RRPlay_Free(rrlog, e);

	if (oldp) {
	    logData(oldp, *oldlenp);
	}
    }

    return result;
}

//XXX: convert
void
__rr_exit(int status)
{
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	syscall(SYS_exit, status);
	__builtin_unreachable();
    }

    if (rrMode == RRMODE_RECORD) {
	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_EXIT;
	RRLog_Append(rrlog, e);
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_EXIT);
	RRPlay_Free(rrlog, e);
    }

    // Drain log
    LogDone();

    syscall(SYS_exit, status);
}

//XXX: convert
int
__socket(int domain, int type, int protocol)
{
    int result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_socket, domain, type, protocol);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_socket, domain, type, protocol);
	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_SOCKET;
	e->value[0] = result;
	RRLog_Append(rrlog, e);
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_SOCKET);
	result = e->value[0];
	RRPlay_Free(rrlog, e);
    }

    return result;
}

//XXX: convert
int
__bind(int s, const struct sockaddr *addr, socklen_t addrlen)
{
    int result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_bind, s, addr, addrlen);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_bind, s, addr, addrlen);
	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_BIND;
	e->value[0] = result;
	RRLog_Append(rrlog, e);
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_BIND);
	result = e->value[0];
	RRPlay_Free(rrlog, e);
    }

    return result;
}

//XXX: convert
int
__listen(int s, int backlog)
{
    int result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_listen, s, backlog);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_listen, s, backlog);
	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_LISTEN;
	e->value[0] = result;
	RRLog_Append(rrlog, e);
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_LISTEN);
	result = e->value[0];
	RRPlay_Free(rrlog, e);
    }

    return result;
}

//XXX: convert
int
__connect(int s, const struct sockaddr *name, socklen_t namelen)
{
    int result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_connect, s, name, namelen);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_connect, s, name, namelen);
	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_CONNECT;
	e->value[0] = result;
	RRLog_Append(rrlog, e);
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_CONNECT);
	result = e->value[0];
	RRPlay_Free(rrlog, e);
    }

    return result;
}

//XXX: convert
int
__accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    int result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_accept, s, addr, addrlen);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_accept, s, addr, addrlen);
	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_ACCEPT;
	e->value[0] = result;
	e->value[1] = *addrlen;
	RRLog_Append(rrlog, e);

	if (result >= 0) {
	    logData((uint8_t *)addr, *addrlen);
	}
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_ACCEPT);
	result = e->value[0];
	if (result >= 0) {
	    *addrlen = e->value[1];
	}
	RRPlay_Free(rrlog, e);

	if (result >= 0) {
	    logData((uint8_t *)addr, *addrlen);
	}
    }

    return result;
}

//XXX: convert
int
__poll(struct pollfd fds[], nfds_t nfds, int timeout)
{
    int result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_poll, fds, nfds, timeout);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_poll, fds, nfds, timeout);
	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_POLL;
	e->value[0] = result;
	e->value[1] = nfds;
	RRLog_Append(rrlog, e);
	if (result > 0) {
	    logData((uint8_t *)fds, nfds * sizeof(struct pollfd));
	}
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_POLL);
	result = e->value[0];
	assert(e->value[1] == nfds);
	RRPlay_Free(rrlog, e);
	if (result > 0) {
	    logData((uint8_t *)fds, nfds * sizeof(struct pollfd));
	}
    }

    return result;
}

//XXX: convert
int
__getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen)
{
    int result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_getsockopt, s, level, optname, optval, optlen);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_getsockopt, s, level, optname, optval, optlen);
	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_GETSOCKOPT;
	e->value[0] = result;
	e->value[1] = optname;
	e->value[2] = *optlen;
	RRLog_Append(rrlog, e);
	if (result > 0) {
	    logData((uint8_t *)optval, *optlen);
	}
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_GETSOCKOPT);
	result = e->value[0];
	*optlen = e->value[2];
	RRPlay_Free(rrlog, e);
	if (result > 0) {
	    logData((uint8_t *)optval, *optlen);
	}
    }

    return result;
}

//XXX: convert
int
__setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)
{
    int result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_setsockopt, s, level, optname, optval, optlen);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_setsockopt, s, level, optname, optval, optlen);
	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_SETSOCKOPT;
	e->value[0] = result;
	e->value[1] = optname;
	e->value[2] = optlen;
	RRLog_Append(rrlog, e);
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_SETSOCKOPT);
	result = e->value[0];
	RRPlay_Free(rrlog, e);
    }

    return result;
}

//XXX: convert
int
__kqueue()
{
    int result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_kqueue);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_kqueue);
	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_KQUEUE;
	e->value[0] = result;
	RRLog_Append(rrlog, e);
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_KQUEUE);
	result = e->value[0];
	RRPlay_Free(rrlog, e);
    }

    return result;
}

//XXX: convert
int
__kevent(int kq, const struct kevent *changelist, int nchanges,
	struct kevent *eventlist, int nevents,
	const struct timespec *timeout)
{
    int result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_kevent, kq, changelist, nchanges,
		       eventlist, nevents, timeout);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_kevent, kq, changelist, nchanges,
			 eventlist, nevents, timeout);
	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_KEVENT;
	e->objectId = kq;
	e->value[0] = result;
	e->value[1] = nchanges;
	e->value[2] = nevents;
	RRLog_Append(rrlog, e);
	if (result > 0) {
	    logData((uint8_t *)eventlist, sizeof(struct kevent) * result);
	}
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_KEVENT);
	result = e->value[0];
	RRPlay_Free(rrlog, e);
	if (result > 0) {
	    logData((uint8_t *)eventlist, sizeof(struct kevent) * result);
	}
    }

    return result;
}

//XXX: convert
void *
__rr_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
    void *result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return __sys_mmap(addr, len, prot, flags, fd, offset);
    }

    // Ignore anonymous maps
    if (fd == -1) {
	return __sys_mmap(addr, len, prot, flags, fd, offset);
    }

    if (rrMode == RRMODE_RECORD) {
	result = __sys_mmap(addr, len, prot, flags, fd, offset);
	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_MMAPFD;
	e->value[0] = (uint64_t)result;
	e->value[1] = len;
	e->value[2] = prot;
	e->value[3] = flags;
	RRLog_Append(rrlog, e);

	if (result != 0) {
	    logData((uint8_t *)result, len);
	}
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_MMAPFD);
	RRPlay_Free(rrlog, e);

	result = __sys_mmap(addr, len, prot, flags | MAP_ANON, -1, 0);
	logData(result, len);
    }

    return result;
}

//XXX: convert
int
__sys_getgroups(int gidsetlen, gid_t *gidset)
{
    int result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_getgroups, gidsetlen, gidset);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_getgroups, gidsetlen, gidset);

	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_GETGROUPS;
	e->objectId = 0;
	e->value[0] = result;
	RRLog_Append(rrlog, e);
	if (result > 0) {
	    logData((uint8_t *)gidset, result * sizeof(gid_t));
	}
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_GETGROUPS);
	result = e->value[0];
	RRPlay_Free(rrlog, e);
	if (result > 0) {
	    logData((uint8_t *)gidset, result * sizeof(gid_t));
	}
    }

    return result;
}

//XXX: convert
int
__sys_setgroups(int ngroups, const gid_t *gidset)
{
    int result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(SYS_setgroups, ngroups, gidset);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(SYS_setgroups, ngroups, gidset);

	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_SETGROUPS;
	e->objectId = 0;
	e->value[0] = result;
	RRLog_Append(rrlog, e);
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_SETGROUPS);
	result = e->value[0];
	RRPlay_Free(rrlog, e);
    }

    return result;
}

//XXX: convert all
static inline int
id_set(int syscallNum, uint32_t eventNum, id_t id)
{
    int result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(syscallNum, id);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(syscallNum, id);

	e = RRLog_Alloc(rrlog, threadId);
	e->event = eventNum;
	e->objectId = id;
	e->value[0] = result;
	RRLog_Append(rrlog, e);
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, eventNum);
	result = e->value[0];
	RRPlay_Free(rrlog, e);
    }

    return result;
}

int
__sys_setuid(uid_t uid)
{
    return id_set(SYS_setuid, RREVENT_SETUID, uid);
}

int
__sys_seteuid(uid_t euid)
{
    return id_set(SYS_seteuid, RREVENT_SETEUID, euid);
}

int
__sys_setgid(gid_t gid)
{
    return id_set(SYS_setgid, RREVENT_SETGID, gid);
}

int
__sys_setegid(gid_t egid)
{
    return id_set(SYS_setegid, RREVENT_SETEGID, egid);
}

static inline id_t
id_get(int syscallNum, uint32_t eventNum)
{
    int result;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return syscall(syscallNum);
    }

    if (rrMode == RRMODE_RECORD) {
	result = syscall(syscallNum);

	e = RRLog_Alloc(rrlog, threadId);
	e->event = eventNum;
	e->objectId = 0;
	e->value[0] = result;
	RRLog_Append(rrlog, e);
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, eventNum);
	result = e->value[0];
	RRPlay_Free(rrlog, e);
    }
    return result;
}

uid_t
__sys_getuid(void)
{
    return id_get(SYS_getuid, RREVENT_GETUID);
}

uid_t
__sys_geteuid(void)
{
    return id_get(SYS_geteuid, RREVENT_GETEUID);
}

gid_t
__sys_getgid(void)
{
    return id_get(SYS_getgid, RREVENT_GETGID);
}

gid_t
__sys_getegid(void)
{
    return id_get(SYS_getegid, RREVENT_GETEGID);
}

int
__sys_link(const char *name1, const char *name2)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_link, name1, name2);
	case RRMODE_RECORD:
	    result = syscall(SYS_link, name1, name2);
	    RRRecordI(RREVENT_LINK, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayI(RREVENT_LINK, &result);
	    break;
    }

    return result;
}

ssize_t
pread(int fd, void *buf, size_t nbytes, off_t offset)
{
    ssize_t result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_pread, fd, buf, nbytes, offset);
	case RRMODE_RECORD:
	    result = syscall(SYS_pread, fd, buf, nbytes, offset);
	    RRRecordOS(RREVENT_PREAD, fd, result);
	    if (result != -1) {
		logData((uint8_t*)buf, nbytes );
	    }
	    break;
	case RRMODE_REPLAY:
	    RRReplayOS(RREVENT_PREAD, &fd, &result);
	    if (result != -1) {
		logData((uint8_t*)buf, nbytes );
	    }
	    break;
    }

    return result;
}


int
__sys_symlink(const char *name1, const char *name2)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_symlink, name1, name2);
	case RRMODE_RECORD:
	    result = syscall(SYS_symlink, name1, name2);
	    RRRecordI(RREVENT_SYMLINK, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayI(RREVENT_SYMLINK, &result);
	    break;
    }

    return result;
}

int
__sys_unlink(const char *path)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_unlink, path);
	case RRMODE_RECORD:
	    result = syscall(SYS_unlink, path);
	    RRRecordI(RREVENT_UNLINK, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayI(RREVENT_UNLINK, &result);
	    break;
    }

    return result;
}

int
__sys_rename(const char *name1, const char *name2)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_rename, name1, name2);
	case RRMODE_RECORD:
	    result = syscall(SYS_rename, name1, name2);
	    RRRecordI(RREVENT_RENAME, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayI(RREVENT_RENAME, &result);
	    break;
    }

    return result;
}

int
__sys_mkdir(const char *path, mode_t mode)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_mkdir, path, mode);
	case RRMODE_RECORD:
	    result = syscall(SYS_mkdir, path, mode);
	    RRRecordI(RREVENT_MKDIR, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayI(RREVENT_MKDIR, &result);
	    break;
    }

    return result;
}

int
__sys_rmdir(const char *path)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_rmdir, path);
	case RRMODE_RECORD:
	    result = syscall(SYS_rmdir, path);
	    RRRecordI(RREVENT_RMDIR, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayI(RREVENT_RMDIR, &result);
	    break;
    }

    return result;
}

int
__sys_chmod(const char *path, mode_t mode)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_chmod, path, mode);
	case RRMODE_RECORD:
	    result = syscall(SYS_chmod, path, mode);
	    RRRecordI(RREVENT_CHMOD, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayI(RREVENT_CHMOD, &result);
	    break;
    }

    return result;
}

int
__sys_access(const char *path, int mode)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_access, path, mode);
	case RRMODE_RECORD:
	    result = syscall(SYS_access, path, mode);
	    RRRecordI(RREVENT_ACCESS, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayI(RREVENT_ACCESS, &result);
	    break;
    }

    return result;
}

int
__rr_truncate(const char *path, off_t length)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_truncate, path, length);
	case RRMODE_RECORD:
	    result = syscall(SYS_truncate, path, length);
	    RRRecordI(RREVENT_TRUNCATE, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayI(RREVENT_TRUNCATE, &result);
	    break;
    }

    return result;
}

int
__rr_ftruncate(int fd, off_t length)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_ftruncate, fd, length);
	case RRMODE_RECORD:
	    result = syscall(SYS_ftruncate, fd, length);
	    RRRecordOI(RREVENT_FTRUNCATE, fd, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayOI(RREVENT_FTRUNCATE, &fd, &result);
	    break;
    }

    return result;
}

int
__rr_flock(int fd, int operation)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_flock, fd, operation);
	case RRMODE_RECORD:
	    result = syscall(SYS_flock, fd, operation);
	    RRRecordOI(RREVENT_FLOCK, fd, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayOI(RREVENT_FLOCK, &fd, &result);
	    break;
    }

    return result;
}

int
__rr_fsync(int fd)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_fsync, fd);
	case RRMODE_RECORD:
	    result = syscall(SYS_fsync, fd);
	    RRRecordOI(RREVENT_FSYNC, fd, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayOI(RREVENT_FSYNC, &fd, &result);
	    break;
    }

    return result;
}

off_t
__rr_lseek(int fildes, off_t offset, int whence)
{
    off_t result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_lseek, fildes, offset, whence);
	case RRMODE_RECORD:
	    result = syscall(SYS_lseek, fildes, offset, whence);
	    RRRecordOS(RREVENT_LSEEK, fildes, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayOS(RREVENT_LSEEK, &fildes, &result);
	    break;
    }

    return result;
}

int
__sys_chdir(const char *path)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_chdir, path);
	case RRMODE_RECORD:
	    result = syscall(SYS_chdir, path);
	    RRRecordI(RREVENT_CHDIR, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayI(RREVENT_CHDIR, &result);
	    break;
    }

    return result;
}

int
__sys_fchdir(int fd)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_fchdir, fd);
	case RRMODE_RECORD:
	    result = syscall(SYS_fchdir, fd);
	    RRRecordOI(RREVENT_FCHDIR, fd, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayOI(RREVENT_FCHDIR, &fd, &result);
	    break;
    }

    return result;
}

int
__rr_lstat(const char *path, struct stat *sb)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_lstat, path, sb);
	case RRMODE_RECORD:
	    result = syscall(SYS_lstat, path, sb);
	    RRRecordI(RREVENT_LSTAT, result);
	    if (result == 0) {
		logData((uint8_t*)sb, sizeof(*sb));
	    }
	    break;
	case RRMODE_REPLAY:
	    RRReplayI(RREVENT_LSTAT, &result);
	    if (result == 0) {
		logData((uint8_t*)sb, sizeof(*sb));
	    }
	    break;
    }

    return result;
}

mode_t
__sys_umask(mode_t numask)
{
    mode_t result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_umask, numask);
	case RRMODE_RECORD:
	    result = syscall(SYS_umask, numask);
	    RRRecordOU(RREVENT_UMASK, 0, (uint64_t)result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayOU(RREVENT_UMASK, NULL, (uint64_t *)&result);
	    break;
    }

    return result;
}

int
__rr_fstatat(int fd, const char *path, struct stat *buf, int flag)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_fstatat, fd, path, buf, flag);
	case RRMODE_RECORD:
	    result = syscall(SYS_fstatat, fd, path, buf, flag);
	    RRRecordOI(RREVENT_FSTATAT, fd, result);
	    if (result == 0) {
		logData((uint8_t*)buf, sizeof(*buf));
	    }
	    break;
	case RRMODE_REPLAY:
	    RRReplayOI(RREVENT_FSTATAT, &fd, &result);
	    if (result == 0) {
		logData((uint8_t*)buf, sizeof(*buf));
	    }
	    break;
    }

    return result;
}


int
__rr_fstat(int fd, struct stat *sb)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_fstat, fd, sb);
	case RRMODE_RECORD:
	    result = syscall(SYS_fstat, fd, sb);
	    RRRecordOI(RREVENT_FSTAT, fd, result);
	    if (result == 0) {
		logData((uint8_t*)sb, sizeof(*sb));
	    }
	    break;
	case RRMODE_REPLAY:
	    RRReplayOI(RREVENT_FSTAT, &fd, &result);
	    if (result == 0) {
		logData((uint8_t*)sb, sizeof(*sb));
	    }
	    break;
    }

    return result;
}

int
__rr_statfs(const char * path, struct statfs *buf)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_statfs, path, buf);
	case RRMODE_RECORD:
	    result = syscall(SYS_statfs, path, buf);
	    RRRecordI(RREVENT_STATFS, result);
	    if (result == 0) {
		logData((uint8_t*)buf, sizeof(*buf));
	    }
	    break;
	case RRMODE_REPLAY:
	    RRReplayI(RREVENT_STATFS, &result);
	    if (result == 0) {
		logData((uint8_t*)buf, sizeof(*buf));
	    }
	    break;
    }

    return result;
}


int
__rr_fstatfs(int fd, struct statfs *buf)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_fstatfs, fd, buf);
	case RRMODE_RECORD:
	    result = syscall(SYS_fstatfs, fd, buf);
	    RRRecordOI(RREVENT_FSTATFS, fd, result);
	    if (result == 0) {
		logData((uint8_t*)buf, sizeof(*buf));
	    }
	    break;
	case RRMODE_REPLAY:
	    RRReplayOI(RREVENT_FSTATFS, &fd, &result);
	    if (result == 0) {
		logData((uint8_t*)buf, sizeof(*buf));
	    }
	    break;
    }

    return result;
}

int
__rr_stat(const char * restrict path, struct stat * restrict sb)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_stat, path, sb);
	case RRMODE_RECORD:
	    result = syscall(SYS_stat, path, sb);
	    RRRecordI(RREVENT_STAT, result);
	    if (result == 0) {
		logData((uint8_t*)sb, sizeof(*sb));
	    }
	    break;
	case RRMODE_REPLAY:
	    RRReplayI(RREVENT_STAT, &result);
	    if (result == 0) {
		logData((uint8_t*)sb, sizeof(*sb));
	    }
	    break;
    }

    return result;
}

int
__sys_getrlimit(int resource, struct rlimit *rlp)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_getrlimit, resource, rlp);
	case RRMODE_RECORD:
	    result = syscall(SYS_getrlimit, resource, rlp);
	    RRRecordI(RREVENT_GETRLIMIT, result);
	    if (result == 0) {
		logData((uint8_t*)rlp, sizeof(*rlp));
	    }
	    break;
	case RRMODE_REPLAY:
	    RRReplayI(RREVENT_GETRLIMIT, &result);
	    if (result == 0) {
		logData((uint8_t*)rlp, sizeof(*rlp));
	    }
	    break;
    }

    return result;
}

int
__sys_setrlimit(int resource, const struct rlimit *rlp)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_setrlimit, resource, rlp);
	case RRMODE_RECORD:
	    result = syscall(SYS_setrlimit, resource, rlp);
	    RRRecordI(RREVENT_SETRLIMIT, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayI(RREVENT_SETRLIMIT, &result);
	    break;
    }

    return result;
}

int
__sys_getrusage(int who, struct rusage *rusage)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_getrusage, who, rusage);
	case RRMODE_RECORD:
	    result = syscall(SYS_getrusage, who, rusage);
	    RRRecordI(RREVENT_GETRUSAGE, result);
	    if (result == 0) {
		logData((uint8_t*)rusage, sizeof(*rusage));
	    }
	    break;
	case RRMODE_REPLAY:
	    RRReplayI(RREVENT_GETRUSAGE, &result);
	    if (result == 0) {
		logData((uint8_t*)rusage, sizeof(*rusage));
	    }
	    break;
    }

    return result;
}

int
__rr_getpeername(int s, struct sockaddr * restrict name,
		 socklen_t * restrict namelen)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_getpeername, s, name, namelen);
	case RRMODE_RECORD:
	    result = syscall(SYS_getpeername, s, name, namelen);
	    RRRecordOI(RREVENT_GETPEERNAME, s, result);
	    if (result == 0) {
		//XXX: slow, store in value[] directly
		logData((uint8_t*)namelen, sizeof(*namelen));
		logData((uint8_t*)name, *namelen);
	    }
	    break;
	case RRMODE_REPLAY:
	    RRReplayOI(RREVENT_GETPEERNAME, &s, &result);
	    if (result == 0) {
		//XXX: slow, store in value[] directly
		logData((uint8_t*)namelen, sizeof(*namelen));
		logData((uint8_t*)name, *namelen);
	    }
	    break;
    }

    return result;
}

int
__rr_getsockname(int s, struct sockaddr * restrict name,
		 socklen_t * restrict namelen)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_getsockname, s, name, namelen);
	case RRMODE_RECORD:
	    result = syscall(SYS_getsockname, s, name, namelen);
	    RRRecordOI(RREVENT_GETSOCKNAME, s, result);
	    if (result == 0) {
		logData((uint8_t*)namelen, sizeof(*namelen));
		logData((uint8_t*)name, *namelen);
		//XXX:slow, store directly in value
	    }
	    break;
	case RRMODE_REPLAY:
	    RRReplayOI(RREVENT_GETSOCKNAME, &s, &result);
	    if (result == 0) {
		logData((uint8_t*)namelen, sizeof(*namelen));
		logData((uint8_t*)name, *namelen);
		//XXX:slow, store directly in value
	    }
	    break;
    }

    return result;
}

int
__rr_cap_enter(void)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_cap_enter);
	case RRMODE_RECORD:
	    result = syscall(SYS_cap_enter);
	    RRRecordI(RREVENT_CAP_ENTER, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayI(RREVENT_CAP_ENTER, &result);
	    break;
    }

    return result;
}

int
__rr_cap_rights_limit(int fd, const cap_rights_t *rights)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_cap_rights_limit, fd, rights);
	case RRMODE_RECORD:
	    result = syscall(SYS_cap_rights_limit, fd, rights);
	    RRRecordOI(RREVENT_CAP_RIGHTS_LIMIT, fd, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayOI(RREVENT_CAP_RIGHTS_LIMIT, &fd, &result);
	    break;
    }

    return result;
}

ssize_t
__rr_sendmsg(int s, const struct msghdr *msg, int flags)
{
    ssize_t result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_sendmsg, s, msg, flags);
	case RRMODE_RECORD:
	    result = syscall(SYS_sendmsg, s, msg, flags);
	    RRRecordOS(RREVENT_SENDMSG, s, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayOS(RREVENT_SENDMSG, &s, &result);
	    break;
    }

    return result;
}

void log_msg(struct msghdr *msg)
{
    logData((uint8_t*)msg, sizeof(*msg));

    if ((msg->msg_name != NULL) && (msg->msg_namelen > 0)) {
	logData((uint8_t*)msg->msg_name, msg->msg_namelen);
    }

    if ((msg->msg_iov != NULL) && (msg->msg_iovlen > 0)) {
	logData((uint8_t*)msg->msg_iov, msg->msg_iovlen * sizeof(*msg->msg_iov));
	for (int i = 0; i < msg->msg_iovlen; i++) {
	    logData(msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len);
	}
    }
    if ((msg->msg_control != NULL) && (msg->msg_controllen > 0)) {
	logData((uint8_t*)msg->msg_control, msg->msg_controllen);
    }
}

ssize_t
__rr_recvmsg(int s, struct msghdr *msg, int flags)
{
    ssize_t result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_recvmsg, s, msg, flags);
	case RRMODE_RECORD:
	    result = syscall(SYS_recvmsg, s, msg, flags);
	    RRRecordOS(RREVENT_RECVMSG, s, result);
	    if (result != -1) {
		log_msg(msg);
	    }
	    break;
	case RRMODE_REPLAY:
	    RRReplayOS(RREVENT_RECVMSG, &s, &result);
	    if (result != -1) {
		log_msg(msg);
	    }
	    break;
    }

    return result;
}


ssize_t
__rr_sendto(int s, const void *msg, size_t len, int flags,
		 const struct sockaddr *to, socklen_t tolen)
{
    ssize_t result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_sendto, s, msg, len, flags, to, tolen);
	case RRMODE_RECORD:
	    result = syscall(SYS_sendto, s, msg, len, flags, to, tolen);
	    RRRecordOS(RREVENT_SENDTO, s, result);
	    break;
	case RRMODE_REPLAY:
	    RRReplayOS(RREVENT_SENDTO, &s, &result);
	    break;
    }

    return result;
}

ssize_t
__rr_recvfrom(int s, void *buf, size_t len, int flags, struct sockaddr *
	restrict from, socklen_t * restrict fromlen)
{
    ssize_t result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_recvfrom, s, buf, len, flags, from, fromlen);
	case RRMODE_RECORD:
	    result = syscall(SYS_recvfrom, s, buf, len, flags, from, fromlen);
	    RRRecordOS(RREVENT_RECVFROM, s, result);
	    if (result != -1) {
		logData((uint8_t*)buf, len);
		if (from != NULL) {
		    logData((uint8_t*)from, sizeof(struct sockaddr));
		    logData((uint8_t*)&fromlen, sizeof(socklen_t));
		}
	    }
	    break;
	case RRMODE_REPLAY:
	    RRReplayOS(RREVENT_RECVFROM, &s, &result);
	    if (result != -1) {
		logData((uint8_t*)buf, len);
		if (from != NULL) {
		    logData((uint8_t*)from, sizeof(struct sockaddr));
		    logData((uint8_t*)&fromlen, sizeof(socklen_t));
		}
	    }
	    break;
    }

    return result;
}


int
__rr_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	 struct timeval *timeout)
{
    int result;

    switch (rrMode) {
	case RRMODE_NORMAL:
	    return syscall(SYS_select, nfds, readfds, writefds, exceptfds, timeout);
	case RRMODE_RECORD:
	    result = syscall(SYS_select, nfds, readfds, writefds, exceptfds, timeout);
	    RRRecordI(RREVENT_SELECT, result);
	    if (result != -1) {
		if (readfds != NULL) {
		    logData((uint8_t*)readfds, sizeof(*readfds));
		}
		if (writefds != NULL) {
		    logData((uint8_t*)writefds, sizeof(*writefds));
		}
		if (exceptfds != NULL) {
		    logData((uint8_t*)exceptfds, sizeof(*exceptfds));
		}
	    }
	    break;
	case RRMODE_REPLAY:
	    RRReplayI(RREVENT_SELECT, &result);
	    if (result != -1) {
		if (readfds != NULL) {
		    logData((uint8_t*)readfds, sizeof(*readfds));
		}
		if (writefds != NULL) {
		    logData((uint8_t*)writefds, sizeof(*writefds));
		}
		if (exceptfds != NULL) {
		    logData((uint8_t*)exceptfds, sizeof(*exceptfds));
		}
	    }
	    break;
    }

    return result;
}

void
Events_Init()
{
    __libc_interposing[INTERPOS_fork] = (interpos_func_t)&__rr_fork;
    __libc_interposing[INTERPOS_read] = (interpos_func_t)&__sys_read;
    __libc_interposing[INTERPOS_write] = (interpos_func_t)&__sys_write;
    __libc_interposing[INTERPOS_openat] = (interpos_func_t)&__rr_openat;
}

__strong_reference(__sys_open, _open);
__strong_reference(__rr_openat, openat);
__strong_reference(__rr_openat, _openat);
__strong_reference(__sys_close, _close);
__strong_reference(__sys_ioctl, ioctl);
__strong_reference(__sys_ioctl, _ioctl);
__strong_reference(__rr_fcntl, fcntl);
__strong_reference(__rr_stat, stat);
__strong_reference(__rr_stat, _stat);
__strong_reference(__rr_fstat, fstat);
__strong_reference(__rr_fstat, _fstat);
__strong_reference(__rr_statfs, statfs);
__strong_reference(__rr_fstatfs, fstatfs);
__strong_reference(__rr_fstatfs, _fstatfs);
__strong_reference(__rr_fstatat, fstatat);
__strong_reference(__rr_fstatat, _fstatat);
__strong_reference(__rr_getdirentries, getdirentries);
__strong_reference(__rr_getdirentries, _getdirentries);
__strong_reference(__clock_gettime, clock_gettime);
__strong_reference(__rr_sysctl, __sysctl);
__strong_reference(__rr_exit, _exit);
__strong_reference(__socket, socket);
__strong_reference(__bind, bind);
__strong_reference(__listen, listen);
__strong_reference(__accept, accept);
__strong_reference(__connect, connect);
__strong_reference(__poll, poll);
__strong_reference(__getsockopt, getsockopt);
__strong_reference(__setsockopt, setsockopt);
__strong_reference(__kqueue, kqueue);
__strong_reference(__kevent, kevent);
__strong_reference(__rr_mmap, mmap);
__strong_reference(_pthread_mutex_lock, pthread_mutex_lock);
__strong_reference(__sys_getuid, getuid);
__strong_reference(__sys_geteuid, geteuid);
__strong_reference(__sys_getgid, getgid);
__strong_reference(__sys_getegid, getegid);
__strong_reference(__sys_setuid, setuid);
__strong_reference(__sys_seteuid, seteuid);
__strong_reference(__sys_setgid, setgid);
__strong_reference(__sys_setegid, setegid);
__strong_reference(__sys_getgroups, getgroups);
__strong_reference(__sys_setgroups, setgroups);
__strong_reference(__sys_link, link);
__strong_reference(__rr_readlink, readlink);
__strong_reference(__rr_readlink, _readlink);
__strong_reference(__sys_symlink, symlink);
__strong_reference(__sys_unlink, unlink);
__strong_reference(__sys_rename, rename);
__strong_reference(__sys_mkdir, mkdir);
__strong_reference(__sys_rmdir, rmdir);
__strong_reference(__sys_chdir, chdir);
__strong_reference(__sys_fchdir, fchdir);
__strong_reference(__sys_chmod, chmod);
__strong_reference(__sys_access, access);
__strong_reference(__rr_truncate, truncate);
__strong_reference(__rr_ftruncate, ftruncate);
__strong_reference(__rr_flock, flock);
__strong_reference(__rr_fsync, fsync);
__strong_reference(__rr_lseek, lseek);
__strong_reference(__rr_lstat, lstat);
__strong_reference(__sys_umask, umask);
__strong_reference(__sys_getrlimit, getrlimit);
__strong_reference(__sys_setrlimit, setrlimit);
__strong_reference(__sys_getrusage, getrusage);
__strong_reference(__rr_getpeername, getpeername);
__strong_reference(__rr_getsockname, getsockname);
__strong_reference(__rr_cap_enter, cap_enter);
__strong_reference(__rr_cap_enter, _cap_enter);
__strong_reference(__rr_cap_rights_limit, cap_rights_limit);
__strong_reference(__rr_cap_rights_limit, _cap_rights_limit);
__strong_reference(__rr_sendto, sendto);
__strong_reference(__rr_sendto, _sendto);
__strong_reference(__rr_sendmsg, sendmsg);
__strong_reference(__rr_sendmsg, _sendmsg);
__strong_reference(__rr_select, _select);
__strong_reference(__rr_select, select);
__strong_reference(__rr_recvmsg, recvmsg);
__strong_reference(__rr_recvmsg, _recvmsg);
__strong_reference(__rr_recvfrom, recvfrom);
__strong_reference(__rr_recvfrom, _recvfrom);
