
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

#include <sys/cdefs.h>
#include <sys/syscall.h>

// wait/waitpid
#include <sys/wait.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <libc_private.h>

#include <castor/debug.h>
#include <castor/rrlog.h>
#include <castor/rrplay.h>
#include <castor/rrgq.h>
#include <castor/mtx.h>
#include <castor/events.h>

#include "util.h"

extern pid_t __sys_getpid();
extern pid_t __sys_getppid();

pid_t
__libc_fork(void)
{

    interpos_func_t * forkP = __libc_interposing_slot(INTERPOS_fork);
    return (pid_t) (*forkP)();

}

pid_t
__rr_fork(void)
{
    pid_t result;
    uintptr_t thrNo;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return __libc_fork();
    }

    if (rrMode == RRMODE_RECORD) {
	thrNo = RRShared_AllocThread(rrlog);

	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_FORK;
	e->value[1] = thrNo;
	RRLog_Append(rrlog, e);

	result = __libc_fork();

	if (result == 0) {
	    threadId = thrNo;
	} else {
	    e = RRLog_Alloc(rrlog, threadId);
	    e->event = RREVENT_FORKEND;
	    e->value[0] = (uint64_t)result;
	    if (result == -1) {
		e->value[1] = (uint64_t)errno;
	    }
	    RRLog_Append(rrlog, e);
	}
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_FORK);
	thrNo = e->value[1];
	RRPlay_Free(rrlog, e);

	RRShared_SetupThread(rrlog, thrNo);

	int rstatus = __libc_fork();
	if (rstatus < 0) {
	    PERROR("Unable to fork on replay!");
	}

	if (rstatus != 0) {
	    e = RRPlay_Dequeue(rrlog, threadId);
	    AssertEvent(e, RREVENT_FORKEND);
	    result = (int)e->value[0];
	    if (result == -1) {
		errno = (int)e->value[1];
	    }
	    RRPlay_Free(rrlog, e);
	} else {
	    threadId = thrNo;
	    result = 0;
	}
    }

    return result;
}

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

    syscall(SYS_exit, status);
}

pid_t
__rr_getpid()
{
    pid_t pid;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return __sys_getpid();
    }

    if (rrMode == RRMODE_RECORD) {
	pid = __sys_getpid();
	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_GETPID;
	e->value[0] = (uint64_t)pid;
	RRLog_Append(rrlog, e);
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_GETPID);
	pid = (pid_t)e->value[0];
	RRPlay_Free(rrlog, e);
    }

    return pid;
}

pid_t
__rr_getppid()
{
    pid_t pid;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return __sys_getppid();
    }

    if (rrMode == RRMODE_RECORD) {
	pid = __sys_getppid();
	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_GETPPID;
	e->value[0] = (uint64_t)pid;
	RRLog_Append(rrlog, e);
    } else {
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_GETPPID);
	pid = (pid_t)e->value[0];
	RRPlay_Free(rrlog, e);
    }

    return pid;
}

/*
 * Will remove this function and replace is it with wait4/6 through the
 * interposing table once pidmap is checked in.
 */
pid_t
__rr_wait(int *status)
{
    pid_t pid;
    RRLogEntry *e;

    if (rrMode == RRMODE_NORMAL) {
	return __sys_wait4(WAIT_ANY, status, 0, NULL);
    }

    if (rrMode == RRMODE_RECORD) {
	pid = __sys_wait4(WAIT_ANY, status, 0, NULL);
	e = RRLog_Alloc(rrlog, threadId);
	e->event = RREVENT_WAIT;
	e->value[0] = (uint64_t)pid;
	if (pid == -1) {
	    e->value[1] = (uint64_t)errno;
	}
	if (status != NULL) {
	    e->value[2] = (uint64_t)*status;
	}
	RRLog_Append(rrlog, e);
    } else {
	// XXX: Use waitpid to wait for the correct child
	__sys_wait4(WAIT_ANY, NULL, 0, NULL);
	e = RRPlay_Dequeue(rrlog, threadId);
	AssertEvent(e, RREVENT_WAIT);
	pid = (int)e->value[0];
	if (pid == -1) {
	    errno = (int)e->value[1];
	}
	if (status != NULL) {
	    *status = (int)e->value[2];
	}
	RRPlay_Free(rrlog, e);
    }

    return pid;
}

BIND_REF(exit);
BIND_REF(getpid);
BIND_REF(getppid);
BIND_REF(wait);
BIND_REF(fork);

