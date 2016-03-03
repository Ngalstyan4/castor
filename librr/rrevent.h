
#ifndef __RREVENT_H__
#define __RREVENT_H__

#define RREVENT_TABLE\
    RREVENT(EXIT,		0x55AA55AA)\
    RREVENT(MUTEX_INIT,		0x00010001)\
    RREVENT(MUTEX_DESTROY,	0x00010002)\
    RREVENT(MUTEX_LOCK,		0x00010003)\
    RREVENT(MUTEX_UNLOCK,	0x00010004)\
    RREVENT(READ,		0x00020001)\
    RREVENT(READEND,		0x00020002)

#define RREVENT(_a, _b) RREVENT_##_a = _b,
enum {
    RREVENT_TABLE
};
#undef RREVENT

#endif

