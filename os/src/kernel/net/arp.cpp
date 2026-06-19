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
