#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>

#ifndef __TCC_TIME_T_TYPEDEF
#define __TCC_TIME_T_TYPEDEF
typedef int time_t;
#endif

#ifndef __TCC_CLOCK_T_TYPEDEF
#define __TCC_CLOCK_T_TYPEDEF
typedef int clock_t;
#endif

#define CLOCKS_PER_SEC 1000

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

clock_t clock(void);
time_t time(time_t *t);
int difftime(time_t end, time_t beginning);
time_t mktime(struct tm *tm);
struct tm *localtime(const time_t *t);
struct tm *gmtime(const time_t *t);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);

#endif
