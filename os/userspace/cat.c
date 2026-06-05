#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: cat <file>\n");
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        int fd = open(argv[i], 0);
        if (fd < 0) {
            printf("cat: cannot open '%s'\n", argv[i]);
            continue;
        }

        char buf[128];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            write(1, buf, (size_t)n);
        }
        close(fd);
    }
    return 0;
}
