#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define LINES_PER_PAGE 24

static void more_page(const char* buf, size_t len) {
    size_t pos = 0;
    unsigned long line = 0;
    while (pos < len) {
        size_t start = pos;
        while (pos < len && buf[pos] != '\n') ++pos;
        if (pos < len && buf[pos] == '\n') ++pos;
        ++line;
        if (line >= LINES_PER_PAGE && pos < len) {
            write(1, "--More--", 8);
            char c;
            if (read(0, &c, 1) != 1) break;
            if (c == 'q' || c == 'Q') break;
            write(1, "\r        \r", 9);
            line = 0;
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        char buf[256];
        ssize_t n;
        while ((n = read(0, buf, sizeof(buf))) > 0) {
            write(1, buf, (size_t)n);
        }
        return 0;
    }
    for (int i = 1; i < argc; ++i) {
        int fd = open(argv[i], 0);
        if (fd < 0) {
            printf("more: cannot open '%s'\n", argv[i]);
            continue;
        }
        if (argc > 2) {
            write(1, "::::::::::::::", 14);
            write(1, "\n", 1);
            write(1, argv[i], strlen(argv[i]));
            write(1, "\n", 1);
            write(1, "::::::::::::::", 14);
            write(1, "\n", 1);
        }
        char buf[512];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            more_page(buf, (size_t)n);
        }
        close(fd);
    }
    return 0;
}
