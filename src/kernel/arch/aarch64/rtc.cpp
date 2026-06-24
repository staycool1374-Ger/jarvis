#include <kernel/arch/rtc.hpp>
#include <kernel/arch/hal/io.hpp>

namespace arch {

uint64_t RTC::read_seconds() {
    uint64_t cnt;
    asm volatile("mrs %0, cntpct_el0" : "=r"(cnt));
    uint64_t freq;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    if (freq == 0) return 0;
    return cnt / freq;
}

void RTC::read_time(tm* out) {
    if (!out) return;
    uint64_t secs = read_seconds();

    uint64_t days = secs / 86400;
    uint64_t rem = secs % 86400;

    out->tm_hour = rem / 3600;
    rem %= 3600;
    out->tm_min = rem / 60;
    out->tm_sec = rem % 60;

    int y = 1970;
    while (days >= 365) {
        bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
        days -= leap ? 366 : 365;
        ++y;
    }
    out->tm_year = y - 1900;

    static const uint8_t mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    int m = 0;
    for (; m < 12; ++m) {
        uint8_t d = mdays[m];
        if (m == 1 && leap) d = 29;
        if (days < d) break;
        days -= d;
    }
    out->tm_mon = m;
    out->tm_mday = days + 1;
    out->tm_wday = 0;
    out->tm_yday = 0;
    out->tm_isdst = 0;
}

} // namespace arch
