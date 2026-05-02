#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#ifndef __TCC_TIME_T_TYPEDEF
#define __TCC_TIME_T_TYPEDEF
typedef int time_t;
#endif

struct timeval {
    time_t tv_sec;
    int tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

int gettimeofday(struct timeval *tv, struct timezone *tz);

#endif
