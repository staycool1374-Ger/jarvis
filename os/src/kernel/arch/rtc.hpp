/// @file rtc.hpp
/// @brief CMOS Real-Time Clock (RTC) driver for wall-clock time.

#pragma once

#include <types.hpp>

namespace arch {

/// @brief Time structure compatible with standard C library tm.
struct tm {
    int tm_sec;    // 0-59
    int tm_min;    // 0-59
    int tm_hour;   // 0-23
    int tm_mday;   // 1-31
    int tm_mon;    // 0-11
    int tm_year;   // years since 1900
    int tm_wday;   // 0-6 (Sunday = 0)
    int tm_yday;   // 0-365
    int tm_isdst;  // DST flag
};

/// @brief CMOS RTC driver providing wall-clock time via I/O ports 0x70/0x71.
class RTC {
public:
    /// @brief Initialises the RTC (disables periodic updates,
    /// selects 24-hour mode).
    static void init();

    /// @brief Reads the current Unix timestamp (seconds since 1970-01-01 UTC).
    /// @return Unix timestamp.
    static uint64_t read_seconds();

    /// @brief Reads the current time into a tm struct (UTC).
    /// @param out Pointer to tm struct to fill.
    static void read_time(tm* out);

    /// @brief Converts a BCD (Binary-Coded Decimal) byte to binary.
    /// @param bcd BCD value (0x00-0x99).
    /// @return Binary value.
    static constexpr uint8_t bcd_to_bin(uint8_t bcd) {
        return (bcd & 0x0F) + ((bcd >> 4) * 10);
    }

    /// @brief Converts a binary byte to BCD.
    /// @param bin Binary value (0-99).
    /// @return BCD value.
    static constexpr uint8_t bin_to_bcd(uint8_t bin) {
        return ((bin / 10) << 4) | (bin % 10);
    }

    /// @brief CMOS I/O port addresses.
    static constexpr uint16_t CMOS_INDEX = 0x70;
    static constexpr uint16_t CMOS_DATA  = 0x71;

    /// @brief CMOS register indices.
    enum Register : uint8_t {
        REG_SECONDS      = 0x00,
        REG_MINUTES      = 0x02,
        REG_HOURS        = 0x04,
        REG_DAY_OF_WEEK  = 0x06,
        REG_DAY_OF_MONTH = 0x07,
        REG_MONTH        = 0x08,
        REG_YEAR         = 0x09,
        REG_CENTURY      = 0x32,  // May not exist on all hardware
        REG_STATUS_A     = 0x0A,
        REG_STATUS_B     = 0x0B,
        REG_STATUS_C     = 0x0C,
    };

private:
    /// @brief Reads a CMOS register.
    /// @param reg Register index.
    /// @return Register value.
    static uint8_t read_register(Register reg);

    /// @brief Writes a CMOS register.
    /// @param reg Register index.
    /// @param value Value to write.
    static void write_register(Register reg, uint8_t value);

    /// @brief Waits for RTC update to complete (Status A UIP bit clear).
    static void wait_for_update();

    /// @brief Reads time registers atomically (handles rollover).
    /// @param sec Seconds out.
    /// @param min Minutes out.
    /// @param hour Hours out.
    /// @param day Day out.
    /// @param month Month out.
    /// @param year Year out (full year, e.g., 2024).
    /// @return True if read succeeded.
    static bool read_time_raw(uint8_t& sec, uint8_t& min, uint8_t& hour,
                              uint8_t& day, uint8_t& month, uint16_t& year);

    /// @brief Computes Unix timestamp from date/time components.
    /// @param year Full year (e.g., 2024).
    /// @param month 1-12.
    /// @param day 1-31.
    /// @param hour 0-23.
    /// @param min 0-59.
    /// @param sec 0-59.
    /// @return Unix timestamp.
    static uint64_t make_timestamp(uint16_t year, uint8_t month, uint8_t day,
                                   uint8_t hour, uint8_t min, uint8_t sec);
};

} // namespace arch
