#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char** argv, char** envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    int fd = open("/dev/tty", 0);
    if (fd < 0) {
        const char* msg = "FAIL: open /dev/tty\n";
        write(2, msg, strlen(msg));
    } else {
        const char* msg = "Hello from userspace C!\n";
        write(fd, msg, strlen(msg));
        close(fd);
    }

    return 0;
}
