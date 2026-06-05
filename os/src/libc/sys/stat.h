#pragma once

#include <sys/types.h>

struct stat {
    off_t     st_size;
    mode_t    st_mode;
    unsigned long st_ino;
};

#define S_IFREG  0x8000
#define S_IFDIR  0x4000
#define S_IFCHR  0x2000

int stat(const char* path, struct stat* buf);
int fstat(int fd, struct stat* buf);
