#pragma once

namespace arch {

class Serial {
public:
    static void init();
    static void putchar(char c);
    static char getchar();
    static void puts(const char* str);
};

} // namespace arch
