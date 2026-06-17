#pragma once

#include <types.hpp>

namespace kernel {

class CRC32 {
public:
    static void init();
    static uint32_t update(uint32_t crc, const uint8_t* data, uint64_t len);
    static uint32_t finalize(uint32_t crc);

    static constexpr uint32_t INITIAL = 0xFFFFFFFF;
    static constexpr uint32_t XOR_OUT = 0xFFFFFFFF;

private:
    static constexpr uint32_t POLY = 0xEDB88320;
    static uint32_t table_[256];
    static bool initialized_;
};

}