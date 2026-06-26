extern "C" {

unsigned long __clzdi2(unsigned long val) {
    if (val == 0) return 64;
    unsigned long count = 0;
    if ((val >> 32) == 0) { count += 32; val <<= 32; }
    if ((val >> 48) == 0) { count += 16; val <<= 16; }
    if ((val >> 56) == 0) { count += 8; val <<= 8; }
    if ((val >> 60) == 0) { count += 4; val <<= 4; }
    if ((val >> 62) == 0) { count += 2; val <<= 2; }
    if ((val >> 63) == 0) { count += 1; }
    return count;
}

unsigned long __ctzdi2(unsigned long val) {
    if (val == 0) return 64;
    unsigned long count = 0;
    if ((val & 0xFFFFFFFFUL) == 0) { count += 32; val >>= 32; }
    if ((val & 0xFFFFUL) == 0) { count += 16; val >>= 16; }
    if ((val & 0xFFUL) == 0) { count += 8; val >>= 8; }
    if ((val & 0xFUL) == 0) { count += 4; val >>= 4; }
    if ((val & 0x3UL) == 0) { count += 2; val >>= 2; }
    if ((val & 0x1UL) == 0) { count += 1; }
    return count;
}

}
