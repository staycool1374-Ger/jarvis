/*
 * Jarvis RTOS — Development Roadmap / Kernel Core
 * Copyright (C) 2026 Arnold Hasshold
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/// @file rtc.cpp
/// @brief AArch64 RTC driver using the system generic timer (CNTPCT_EL0).

#include <kernel/arch/rtc.hpp>
#include <kernel/arch/hal/io.hpp>

namespace arch {

/// @brief Read the system timer counter and convert to seconds since epoch.
/// @return Seconds since 1970-01-01 (Unix epoch), or 0 if frequency is unknown.
/// @note Uses CNTPCT_EL0 for the counter and CNTFRQ_EL0 for the tick rate.
uint64_t RTC::read_seconds() {
    uint64_t cnt{};
    asm volatile("mrs %0, cntpct_el0" : "=r"(cnt));
    uint64_t freq{};
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    if (freq == 0)
        return 0;
    return cnt / freq;
}

/// @brief Read and decompose the current time into a tm structure.
/// @param[out] out Pointer to tm struct to fill. If null, returns immediately.
void RTC::read_time(tm *out) {
    if (!out)
        return;
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

    static const uint8_t mdays[12] = {31, 28, 31, 30, 31, 30,
                                      31, 31, 30, 31, 30, 31};
    bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    int m = 0;
    for (; m < 12; ++m) {
        uint8_t d = mdays[m];
        if (m == 1 && leap)
            d = 29;
        if (days < d)
            break;
        days -= d;
    }
    out->tm_mon = m;
    out->tm_mday = days + 1;
    out->tm_wday = 0;
    out->tm_yday = 0;
    out->tm_isdst = 0;
}

} // namespace arch
