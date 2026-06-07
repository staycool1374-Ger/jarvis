#include <kernel/arch/rtc.hpp>
#include <kernel/arch/io.hpp>

namespace arch {

static uint8_t rtc_read_register(uint8_t reg) {
    outb(RTC::CMOS_INDEX, reg | 0x80);  // Set NMI disable bit (bit 7)
    io_wait();
    return inb(RTC::CMOS_DATA);
}

static void rtc_write_register(uint8_t reg, uint8_t value) {
    outb(RTC::CMOS_INDEX, reg | 0x80);
    io_wait();
    outb(RTC::CMOS_DATA, value);
    io_wait();
}

void RTC::init() {
    // Read Status Register B
    uint8_t status_b = rtc_read_register(REG_STATUS_B);

    // Set 24-hour mode (bit 1 = 1), binary mode (bit 2 = 1), disable periodic interrupt (bit 6 = 0)
    status_b |= 0x06;   // 24-hour + binary
    status_b &= ~0x40;  // disable periodic interrupt
    rtc_write_register(REG_STATUS_B, status_b);

    // Read Status Register A
    uint8_t status_a = rtc_read_register(REG_STATUS_A);

    // Disable update-ended interrupt (bit 4 = 0), set frequency to default (bits 0-3)
    status_a &= ~0x10;
    rtc_write_register(REG_STATUS_A, status_a);

    // Clear Status Register C (read to clear)
    (void)rtc_read_register(REG_STATUS_C);
}

uint8_t RTC::read_register(Register reg) {
    return rtc_read_register(static_cast<uint8_t>(reg));
}

void RTC::write_register(Register reg, uint8_t value) {
    rtc_write_register(static_cast<uint8_t>(reg), value);
}

void RTC::wait_for_update() {
    // Wait for UIP (Update In Progress) bit to clear in Status A
    while (rtc_read_register(REG_STATUS_A) & 0x80) {
        io_wait();
    }
}

bool RTC::read_time_raw(uint8_t& sec, uint8_t& min, uint8_t& hour,
                        uint8_t& day, uint8_t& month, uint16_t& year) {
    wait_for_update();

    uint8_t sec1 = rtc_read_register(REG_SECONDS);
    uint8_t min1 = rtc_read_register(REG_MINUTES);
    uint8_t hour1 = rtc_read_register(REG_HOURS);
    uint8_t day1 = rtc_read_register(REG_DAY_OF_MONTH);
    uint8_t month1 = rtc_read_register(REG_MONTH);
    uint8_t year1 = rtc_read_register(REG_YEAR);
    uint8_t century = rtc_read_register(REG_CENTURY);

    // Read again to check for rollover
    wait_for_update();

    uint8_t sec2 = rtc_read_register(REG_SECONDS);
    uint8_t min2 = rtc_read_register(REG_MINUTES);
    uint8_t hour2 = rtc_read_register(REG_HOURS);
    uint8_t day2 = rtc_read_register(REG_DAY_OF_MONTH);
    uint8_t month2 = rtc_read_register(REG_MONTH);
    uint8_t year2 = rtc_read_register(REG_YEAR);

    // If values changed during read, retry (simple approach: just use second read)
    if (sec1 != sec2 || min1 != min2 || hour1 != hour2 ||
        day1 != day2 || month1 != month2 || year1 != year2) {
        sec = sec2;
        min = min2;
        hour = hour2;
        day = day2;
        month = month2;
        year = (century * 100) + year2;
        return true;
    }

    sec = sec1;
    min = min1;
    hour = hour1;
    day = day1;
    month = month1;
    year = (century * 100) + year1;

    // Convert from BCD if RTC is in BCD mode (check Status B bit 2)
    uint8_t status_b = rtc_read_register(REG_STATUS_B);
    if (!(status_b & 0x04)) {  // Bit 2 = 0 means BCD mode
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        hour = bcd_to_bin(hour);
        day = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year = (bcd_to_bin(century) * 100) + bcd_to_bin(year);
    }

    return true;
}

uint64_t RTC::make_timestamp(uint16_t year, uint8_t month, uint8_t day,
                             uint8_t hour, uint8_t min, uint8_t sec) {
    // Days in each month (non-leap year)
    static const uint16_t days_in_month[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    // Validate inputs
    if (year < 1970 || month == 0 || month > 12 || day == 0 || day > 31 ||
        hour > 23 || min > 59 || sec > 59) {
        return 0;
    }

    // Adjust for leap year in February
    uint16_t dim = days_in_month[month - 1];
    if (month == 2) {
        bool leap = (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
        if (leap) dim = 29;
    }
    if (day > dim) return 0;

    // Count days since 1970-01-01
    uint64_t days = 0;

    // Years
    for (uint16_t y = 1970; y < year; ++y) {
        days += 365;
        if ((y % 4 == 0) && (y % 100 != 0 || y % 400 == 0)) {
            days += 1;
        }
    }

    // Months
    for (uint8_t m = 1; m < month; ++m) {
        days += days_in_month[m - 1];
        if (m == 2) {
            bool leap = (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
            if (leap) days += 1;
        }
    }

    // Days (day is 1-based)
    days += day - 1;

    // Convert to seconds
    uint64_t timestamp = days * 86400ULL + hour * 3600ULL + min * 60ULL + sec;

    return timestamp;
}

uint64_t RTC::read_seconds() {
    uint8_t sec, min, hour, day, month;
    uint16_t year;

    if (!read_time_raw(sec, min, hour, day, month, year)) {
        return 0;
    }

    return make_timestamp(year, month, day, hour, min, sec);
}

void RTC::read_time(struct tm* out) {
    if (!out) return;

    uint8_t sec, min, hour, day, month;
    uint16_t year;

    if (!read_time_raw(sec, min, hour, day, month, year)) {
        // Zero out on failure
        out->tm_sec = out->tm_min = out->tm_hour = 0;
        out->tm_mday = out->tm_mon = out->tm_year = 0;
        out->tm_wday = out->tm_yday = out->tm_isdst = 0;
        return;
    }

    out->tm_sec = sec;
    out->tm_min = min;
    out->tm_hour = hour;
    out->tm_mday = day;
    out->tm_mon = month - 1;   // tm_mon is 0-11
    out->tm_year = year - 1900; // tm_year is years since 1900
    out->tm_wday = 0; // Not tracked
    out->tm_yday = 0; // Not computed
    out->tm_isdst = 0; // UTC, no DST
}

} // namespace arch