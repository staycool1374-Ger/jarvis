int main(void) {
    for (;;) {
#if defined(__x86_64__)
        __asm__ volatile("pause");
#elif defined(__aarch64__)
        __asm__ volatile("yield");
#endif
    }
}
