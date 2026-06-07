#pragma once

#ifndef __LIBK_SYSCALL_H
#define __LIBK_SYSCALL_H

#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>

#define SYS_YIELD       0
#define SYS_SEND        1
#define SYS_RECEIVE     2
#define SYS_SEND_SYNC   3
#define SYS_PRINT       4
#define SYS_GET_TICKS   5
#define SYS_EXIT        6
#define SYS_CREATE_MAILBOX 7
#define SYS_DESTROY_MAILBOX 8
#define SYS_OPEN        9
#define SYS_READ        10
#define SYS_CLOSE       11
#define SYS_FSTAT       12
#define SYS_WRITE       13
#define SYS_LSEEK       14
#define SYS_IOCTL       15
#define SYS_READDIR     16
#define SYS_STAT        17
#define SYS_DUP         18
#define SYS_CHDIR       19
#define SYS_EXEC        20
#define SYS_FORK        21
#define SYS_WAITPID     22
#define SYS_GETPID      23
#define SYS_KILL        24
#define SYS_PIPE        25
#define SYS_DUP2        26
#define SYS_NOTIFY      27
#define SYS_NOTIFY_WAIT 28
#define SYS_EVENT_SET   29
#define SYS_EVENT_WAIT  30
#define SYS_SIGNAL      31
#define SYS_SIGRETURN   32
#define SYS_ALARM       33
#define SYS_GETTOD      34
#define SYS_UNAME       35
#define SYS_PAUSE       36

static inline long __syscall5(long num, long a0, long a1, long a2, long a3) {
    long ret;
    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a0), "c"(a1), "d"(a2), "S"(a3)
        : "memory", "cc"
    );
    return ret;
}

static inline long sys_alarm(unsigned int seconds) {
    return __syscall5(SYS_ALARM, seconds, 0, 0, 0);
}

static inline long sys_alarm_us(unsigned int seconds, unsigned int microseconds) {
    return __syscall5(SYS_ALARM, seconds, microseconds, 0, 0);
}

static inline long sys_gettod(struct timeval* tv) {
    return __syscall5(SYS_GETTOD, (long)tv, 0, 0, 0);
}

static inline long sys_uname(struct utsname* buf) {
    return __syscall5(SYS_UNAME, (long)buf, 0, 0, 0);
}

static inline long sys_kill(pid_t pid, int sig) {
    return __syscall5(SYS_KILL, pid, sig, 0, 0);
}

static inline long sys_signal(int signum, void (*handler)(int)) {
    return __syscall5(SYS_SIGNAL, signum, (long)handler, 0, 0);
}

static inline long sys_sigreturn(void) {
    return __syscall5(SYS_SIGRETURN, 0, 0, 0, 0);
}

static inline long sys_pause(void) {
    return __syscall5(SYS_PAUSE, 0, 0, 0, 0);
}

#endif
