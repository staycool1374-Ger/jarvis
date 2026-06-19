/// @file arp.hpp
/// @brief ARP (Address Resolution Protocol) — IPv4 → MAC resolution.

#pragma once

#include <types.hpp>
#include <kernel/net/ether.hpp>

namespace net {

constexpr size_t ARP_CACHE_SIZE = 32;

/// ARP packet header (28 bytes)
struct ArpHeader {
    uint16_t htype;       // hardware type (1 = Ethernet)
    uint16_t ptype;       // protocol type (0x0800 = IPv4)
    uint8_t  hlen;        // hardware address length (6)
    uint8_t  plen;        // protocol address length (4)
    uint16_t oper;        // operation (1 = request, 2 = reply)
    MacAddr  sha;         // sender hardware address
    uint32_t spa;         // sender protocol address (IPv4, big-endian)
    MacAddr  tha;         // target hardware address
    uint32_t tpa;         // target protocol address (IPv4, big-endian)
} __attribute__((packed));

constexpr uint16_t ARP_HTYPE_ETHER = 1;
constexpr uint16_t ARP_OPER_REQUEST = 1;
constexpr uint16_t ARP_OPER_REPLY   = 2;

/// ARP cache entry
struct ArpCacheEntry {
    uint32_t ip;         // IPv4 address (big-endian)
    MacAddr  mac;
    bool     valid;
};

/// ARP cache — resolves IP → MAC, caches results.
class ArpCache {
public:
    ArpCache();

    /// Look up a MAC for the given IP.
    /// @return true if found, populates @p mac.
    bool lookup(uint32_t ip, MacAddr& mac) const;

    /// Update the cache with a new IP→MAC mapping.
    void update(uint32_t ip, MacAddr mac);

    /// Remove an entry.
    void remove(uint32_t ip);

    /// Clear all entries.
    void clear();

    /// Get a pointer to the entry array (for direct iteration).
    const ArpCacheEntry* entries() const { return entries_; }

private:
    ArpCacheEntry entries_[ARP_CACHE_SIZE];

    int find(uint32_t ip) const;
    int find_empty() const;
};

} // namespace net
