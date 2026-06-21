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

/// @file ether.hpp
/// @brief Ethernet frame types and constants.

#pragma once

#include <types.hpp>

namespace net {

constexpr size_t ETH_ADDR_LEN = 6;
constexpr size_t ETH_HEADER_LEN = 14;
constexpr size_t ETH_MTU = 1500;
constexpr size_t ETH_FRAME_MAX = 1518;

/// Ethernet MAC address
struct MacAddr {
    uint8_t addr[ETH_ADDR_LEN];

    bool operator==(const MacAddr& o) const {
        for (size_t i = 0; i < ETH_ADDR_LEN; ++i)
            if (addr[i] != o.addr[i]) return false;
        return true;
    }
    bool operator!=(const MacAddr& o) const { return !(*this == o); }

    bool is_broadcast() const {
        for (size_t i = 0; i < ETH_ADDR_LEN; ++i)
            if (addr[i] != 0xFF) return false;
        return true;
    }
    bool is_null() const {
        for (size_t i = 0; i < ETH_ADDR_LEN; ++i)
            if (addr[i] != 0) return false;
        return true;
    }
} __attribute__((packed));

constexpr MacAddr MAC_BROADCAST = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
constexpr MacAddr MAC_NULL     = {{0, 0, 0, 0, 0, 0}};

/// Ethernet frame header
struct EtherHeader {
    MacAddr  dst;
    MacAddr  src;
    uint16_t type;  // big-endian EtherType
} __attribute__((packed));

/// EtherType values (big-endian)
enum EtherType : uint16_t {
    ETH_TYPE_IPV4 = 0x0800,
    ETH_TYPE_ARP  = 0x0806,
    ETH_TYPE_IPV6 = 0x86DD,
};

} // namespace net
