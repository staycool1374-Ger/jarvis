/// @file udp.hpp
/// @brief UDP protocol types and helpers.

#pragma once

#include <types.hpp>

namespace net {

/// UDP header (8 bytes)
struct UdpHeader {
    uint16_t src_port;   // big-endian
    uint16_t dst_port;   // big-endian
    uint16_t length;     // header + data, big-endian
    uint16_t checksum;   // big-endian
} __attribute__((packed));

} // namespace net
