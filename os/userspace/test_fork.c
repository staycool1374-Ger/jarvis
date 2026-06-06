#include <syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <stddef.h>

#define SYS_EXEC 20

int main(void) {
    char msg[] = "test_fork: starting\n";
    write(1, msg, sizeof(msg) - 1);

    pid_t pid = fork();
    if (pid < 0) {
        char e[] = "test_fork: fork failed\n";
        write(1, e, sizeof(e) - 1);
        _exit(1);
    }

    if (pid == 0) {
        char c[] = "test_fork: child running\n";
        write(1, c, sizeof(c) - 1);

        char* argv[] = {"nonexistent", NULL};
        long r = __syscall5(SYS_EXEC, (long)"nonexistent", (long)argv, 0, 0);
        char buf[64];
        int n = snprintf(buf, sizeof(buf), "test_fork: exec returned %ld\n", r);
        write(1, buf, n);
        _exit(42);
    }

    char p[] = "test_fork: parent waiting\n";
    write(1, p, sizeof(p) - 1);

    int status = 0;
    pid_t r = waitpid(pid, &status, 0);
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "test_fork: waitpid returned %ld, status=0x%x\n", (long)r, status);
    write(1, buf, n);

    _exit(0);
}
