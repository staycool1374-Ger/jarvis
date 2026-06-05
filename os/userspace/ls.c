#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <syscall.h>

struct dirent {
    char d_name[64];
    unsigned long d_ino;
};

int main(int argc, char** argv) {
    const char* path = "/";
    if (argc > 1) path = argv[1];

    int fd = open(path, 0);
    if (fd < 0) {
        printf("ls: cannot open '%s'\n", path);
        return 1;
    }

    unsigned long pos = 0;
    struct dirent dent;

    while (1) {
        long r = __syscall5(16, (long)fd, (long)&pos, (long)&dent, 0);
        if (r < 0) break;
        if (dent.d_name[0] == '\0') continue;
        printf("%s\n", dent.d_name);
    }

    close(fd);
    return 0;
}
