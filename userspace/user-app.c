#include <syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <stddef.h>

int main(void) {
    // NOTE: use a string literal (.rodata) rather than a stack-local buffer.
    // The user-app's initial stack is set up with only part of its stack
    // region mapped; a stack-local would land in an unmapped page and fault
    // the kernel copy path.  A literal lives in a mapped ELF segment.
    static const char msg[] = "user-app: placeholder (no app loaded)\n";
    write(1, msg, sizeof(msg) - 1);
    _exit(0);
}
