#include <syscall.h>
#include <unistd.h>

int open(const char* path, int flags) {
    return (int)__syscall5(9, (long)path, (long)flags, 0, 0);
}

ssize_t read(int fd, void* buf, size_t count) {
    return (ssize_t)__syscall5(10, (long)fd, (long)buf, (long)count, 0);
}

ssize_t write(int fd, const void* buf, size_t count) {
    return (ssize_t)__syscall5(13, (long)fd, (long)buf, (long)count, 0);
}

int close(int fd) {
    return (int)__syscall5(11, (long)fd, 0, 0, 0);
}

off_t lseek(int fd, off_t offset, int whence) {
    return (off_t)__syscall5(14, (long)fd, (long)offset, (long)whence, 0);
}

int dup(int fd) {
    return (int)__syscall5(18, (long)fd, 0, 0, 0);
}

int dup2(int oldfd, int newfd) {
    return (int)__syscall5(26, (long)oldfd, (long)newfd, 0, 0);
}

int chdir(const char* path) {
    return (int)__syscall5(19, (long)path, 0, 0, 0);
}

int pipe(int fds[2]) {
    return (int)__syscall5(25, (long)fds, 0, 0, 0);
}

pid_t fork(void) {
    return (pid_t)__syscall5(21, 0, 0, 0, 0);
}

pid_t waitpid(pid_t pid, int* status, int options) {
    return (pid_t)__syscall5(22, (long)pid, (long)status, (long)options, 0);
}

pid_t getpid(void) {
    return (pid_t)__syscall5(23, 0, 0, 0, 0);
}

void _exit(int status) {
    __syscall5(6, (long)status, 0, 0, 0);
    for (;;) __builtin_trap();
}

int mkdir(const char* path, unsigned int mode) {
    return (int)__syscall5(SYS_MKDIR, (long)path, (long)mode, 0, 0);
}

int unlink(const char* path) {
    return (int)__syscall5(SYS_UNLINK, (long)path, 0, 0, 0);
}
