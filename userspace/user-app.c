#include <syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <stddef.h>

int main(void) {
    char msg[] = "user-app: placeholder (no app loaded)\n";
    write(1, msg, sizeof(msg) - 1);
    _exit(0);
}
