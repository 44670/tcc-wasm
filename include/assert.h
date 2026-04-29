#ifndef _ASSERT_H
#define _ASSERT_H

#ifdef NDEBUG
#define assert(x) ((void)0)
#else
void abort(void);
#define assert(x) ((x) ? (void)0 : abort())
#endif

#endif
