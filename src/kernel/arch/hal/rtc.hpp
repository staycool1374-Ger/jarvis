#pragma once

#include <types.hpp>
#include <kernel/jarvis_config.h>

namespace arch {

#if defined(CONFIG_ARCH_X86_64)

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

class RTC {
public:
    static void init();
    static uint64_t read_seconds();
    static void read_time(tm* out);

    static constexpr uint8_t bcd_to_bin(uint8_t bcd) {
        return (bcd & 0x0F) + ((bcd >> 4) * 10);
    }
    static constexpr uint8_t bin_to_bcd(uint8_t bin) {
        return ((bin / 10) << 4) | (bin % 10);
    }

    static constexpr uint16_t CMOS_INDEX = 0x70;
    static constexpr uint16_t CMOS_DATA  = 0x71;

    enum Register : uint8_t {
        REG_SECONDS      = 0x00,
        REG_MINUTES      = 0x02,
        REG_HOURS        = 0x04,
        REG_DAY_OF_WEEK  = 0x06,
        REG_DAY_OF_MONTH = 0x07,
        REG_MONTH        = 0x08,
        REG_YEAR         = 0x09,
        REG_CENTURY      = 0x32,
        REG_STATUS_A     = 0x0A,
        REG_STATUS_B     = 0x0B,
        REG_STATUS_C     = 0x0C,
    };

private:
    static uint8_t read_register(Register reg);
    static void write_register(Register reg, uint8_t value);
    static void wait_for_update();
    static bool read_time_raw(uint8_t& sec, uint8_t& min, uint8_t& hour,
                              uint8_t& day, uint8_t& month, uint16_t& year);
    static uint64_t make_timestamp(uint16_t year, uint8_t month, uint8_t day,
                                   uint8_t hour, uint8_t min, uint8_t sec);
};

#elif defined(CONFIG_ARCH_AARCH64) || defined(CONFIG_ARCH_RISCV64)

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

class RTC {
public:
    static void init() {}
    static uint64_t read_seconds();
    static void read_time(tm* out);
};

#else
#  error "HAL: no rtc implementation for this architecture"
#endif

} // namespace arch
