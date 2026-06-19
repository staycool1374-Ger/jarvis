#pragma once

#include <types.hpp>

namespace net {

constexpr uint8_t ICMP_TYPE_ECHO_REPLY   = 0;
constexpr uint8_t ICMP_TYPE_ECHO_REQUEST = 8;

constexpr size_t ICMP_HEADER_LEN = 8;

struct IcmpHeader {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t ident;
    uint16_t seq;
} __attribute__((packed));

inline uint16_t icmp_checksum(const uint8_t* data, size_t len) {
    uint32_t sum = 0;
    auto* words = reinterpret_cast<const uint16_t*>(data);
    size_t word_count = len / 2;
    for (size_t i = 0; i < word_count; ++i) {
        sum += words[i];
    }
    if (len & 1) {
        sum += static_cast<uint16_t>(data[len - 1]) << 8;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<uint16_t>(~sum & 0xFFFF);
}

} // namespace net
