#include <syscall.h>
#include <time.h>

time_t time(time_t* tloc) {
    struct timeval tv;
    if (sys_gettod(&tv) < 0) return (time_t)-1;
    if (tloc) *tloc = tv.tv_sec;
    return tv.tv_sec;
}

int gettimeofday(struct timeval* tv, void* tz) {
    (void)tz;
    return (int)sys_gettod(tv);
}

unsigned int sleep(unsigned int seconds) {
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    
    // Use alarm + pause
    unsigned int remaining = sys_alarm(seconds);
    if (remaining > 0) return remaining;
    
    sys_sigreturn();
    sys_pause();
    
    // If we return, alarm fired or was interrupted
    return sys_alarm(0);
}

unsigned int alarm(unsigned int seconds) {
    return (unsigned int)sys_alarm(seconds);
}

int nanosleep(const struct timespec* req, struct timespec* rem) {
    if (!req) return -1;
    
    unsigned int total_us = req->tv_sec * 1000000 + req->tv_nsec / 1000;
    unsigned int secs = total_us / 1000000;
    unsigned int usecs = total_us % 1000000;
    
    unsigned int remaining = sys_alarm_us(secs, usecs);
    if (remaining > 0) {
        if (rem) {
            rem->tv_sec = remaining / 1000000;
            rem->tv_nsec = (remaining % 1000000) * 1000;
        }
        return -1;
    }
    
    sys_pause();
    return 0;
}