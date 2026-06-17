#include <crc32.hpp>

namespace kernel {

uint32_t CRC32::table_[256];
bool CRC32::initialized_ = false;

void CRC32::init() {
    if (initialized_) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ POLY;
            } else {
                crc >>= 1;
            }
        }
        table_[i] = crc;
    }
    initialized_ = true;
}

uint32_t CRC32::update(uint32_t crc, const uint8_t* data, uint64_t len) {
    for (uint64_t i = 0; i < len; ++i) {
        crc = table_[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

uint32_t CRC32::finalize(uint32_t crc) {
    return crc ^ XOR_OUT;
}

}