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

/// @file arp.cpp
/// @brief ARP cache implementation.

#include <kernel/net/arp.hpp>
#include <string.hpp>

namespace net {

ArpCache::ArpCache() {
    clear();
}

bool ArpCache::lookup(uint32_t ip, MacAddr& mac) const {
    for (size_t i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (entries_[i].valid && entries_[i].ip == ip) {
            mac = entries_[i].mac;
            return true;
        }
    }
    return false;
}

void ArpCache::update(uint32_t ip, MacAddr mac) {
    int idx = find(ip);
    if (idx < 0) idx = find_empty();
    if (idx < 0) {
        idx = 0;
    }
    entries_[idx].ip    = ip;
    entries_[idx].mac   = mac;
    entries_[idx].valid = true;
}

void ArpCache::remove(uint32_t ip) {
    int idx = find(ip);
    if (idx >= 0) {
        entries_[idx].valid = false;
    }
}

void ArpCache::clear() {
    memset(entries_, 0, sizeof(entries_));
}

int ArpCache::find(uint32_t ip) const {
    for (size_t i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (entries_[i].valid && entries_[i].ip == ip) return static_cast<int>(i);
    }
    return -1;
}

int ArpCache::find_empty() const {
    for (size_t i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (!entries_[i].valid) return static_cast<int>(i);
    }
    return -1;
}

} // namespace net
