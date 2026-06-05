#pragma once

#ifndef __LIBK_SYSCALL_H
#define __LIBK_SYSCALL_H

#include <sys/types.h>

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

#endif
