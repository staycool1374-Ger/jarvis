#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    int fd = open("/proc/meminfo", 0);
    if (fd >= 0) {
        char buf[128];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            write(1, buf, (size_t)n);
        }
        close(fd);
    }

    write(1, "\nTasks:\n", 8);
    write(1, "(use 'tasks' in kernel shell)\n", 31);

    return 0;
}
