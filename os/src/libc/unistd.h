#pragma once

#include <sys/types.h>
#include <time.h>
#include <sys/utsname.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define O_RDONLY   0
#define O_WRONLY   1
#define O_RDWR     2
#define O_NONBLOCK 0x800

extern char** environ;

pid_t getpid(void);
int open(const char* path, int flags);
ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
int dup(int fd);
int dup2(int oldfd, int newfd);
int chdir(const char* path);
int pipe(int fds[2]);

pid_t fork(void);
pid_t waitpid(pid_t pid, int* status, int options);

void _exit(int status);
unsigned int sleep(unsigned int seconds);
unsigned int alarm(unsigned int seconds);
int gettimeofday(struct timeval* tv, void* tz);
int uname(struct utsname* buf);
int pause(void);
int mkdir(const char* path, unsigned int mode);
int unlink(const char* path);
