#pragma once

#ifndef __LIBK_TIME_H
#define __LIBK_TIME_H

#include <sys/types.h>

#define CLOCKS_PER_SEC 1000000

struct tm {
    int tm_sec;     // seconds [0,60]
    int tm_min;     // minutes [0,59]
    int tm_hour;    // hours [0,23]
    int tm_mday;    // day of month [1,31]
    int tm_mon;     // month [0,11]
    int tm_year;    // years since 1900
    int tm_wday;    // day of week [0,6] (Sunday = 0)
    int tm_yday;    // day of year [0,365]
    int tm_isdst;   // DST flag
};

struct timeval {
    time_t tv_sec;     // seconds
    suseconds_t tv_usec; // microseconds
};

struct timespec {
    time_t tv_sec;   // seconds
    long tv_nsec;    // nanoseconds
};

time_t time(time_t* tloc);
int gettimeofday(struct timeval* tv, void* tz);
int nanosleep(const struct timespec* req, struct timespec* rem);
unsigned int sleep(unsigned int seconds);

#endif // __LIBK_TIME_H