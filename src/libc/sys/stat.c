#include <sys/stat.h>
#include <syscall.h>

int stat(const char* path, struct stat* buf) {
    return (int)__syscall5(17, (long)path, (long)buf, 0, 0);
}

int fstat(int fd, struct stat* buf) {
    return (int)__syscall5(12, (long)fd, (long)buf, 0, 0);
}
