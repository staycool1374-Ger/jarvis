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

#include <test.hpp>
#include <logger.hpp>
#include <kernel/net/ether.hpp>
#include <kernel/net/arp.hpp>
#include <kernel/net/ipv4.hpp>
#include <kernel/net/udp.hpp>
#include <kernel/net/net.hpp>
#include <string.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies MacAddr equality and broadcast/null detection.
// Input: Compare various MAC addresses
// Expect: Equality works, broadcast/null detection works
// Depends: net::MacAddr
JARVIS_TEST(net_mac_address_ops, "PRE: none | POST: none") {
    net::MacAddr a = {{0x52, 0x54, 0x00, 0x12, 0x34, 0x56}};
    net::MacAddr b = {{0x52, 0x54, 0x00, 0x12, 0x34, 0x56}};
    net::MacAddr c = {{0x52, 0x54, 0x00, 0x12, 0x34, 0x57}};
    JARVIS_ASSERT(a == b);
    JARVIS_ASSERT(a != c);
    JARVIS_ASSERT(!a.is_broadcast());
    JARVIS_ASSERT(!a.is_null());
    JARVIS_ASSERT(net::MAC_BROADCAST.is_broadcast());
    JARVIS_ASSERT(net::MAC_NULL.is_null());
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies Ipv4Addr from_u32 and as_u32 round-trip.
// Input: net::Ipv4Addr::from_u32(0xC0A80101) -> as_u32()
// Expect: as_u32 returns 0xC0A80101
// Depends: net::Ipv4Addr
JARVIS_TEST(net_ipv4_addr_roundtrip, "PRE: none | POST: none") {
    uint32_t ip = 0xC0A80101; // 192.168.1.1
    net::Ipv4Addr addr = net::Ipv4Addr::from_u32(ip);
    JARVIS_ASSERT_EQ(ip, addr.as_u32());
    JARVIS_ASSERT_EQ(192, addr.addr[0]);
    JARVIS_ASSERT_EQ(168, addr.addr[1]);
    JARVIS_ASSERT_EQ(1, addr.addr[2]);
    JARVIS_ASSERT_EQ(1, addr.addr[3]);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies ARP cache add, lookup, remove, and clear operations.
// Input: net::ArpCache
// Expect: add + lookup returns correct entry, remove removes it, clear clears all
// Depends: net::ArpCache, net::MacAddr
JARVIS_TEST(net_arp_cache_ops, "PRE: none | POST: none") {
    net::ArpCache cache;
    net::MacAddr mac = {{0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01}};
    uint32_t ip = 0xC0A80164; // 192.168.1.100

    net::MacAddr out;
    JARVIS_ASSERT(!cache.lookup(ip, out));

    cache.update(ip, mac);
    JARVIS_ASSERT(cache.lookup(ip, out));
    JARVIS_ASSERT(out == mac);

    cache.remove(ip);
    JARVIS_ASSERT(!cache.lookup(ip, out));

    cache.update(ip, mac);
    cache.clear();
    JARVIS_ASSERT(!cache.lookup(ip, out));
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies IPv4 header checksum calculation on a known header.
// Input: Build a minimal IPv4 header, compute checksum
// Expect: Checksum matches known value
// Depends: net::ipv4_checksum
JARVIS_TEST(net_ipv4_checksum, "PRE: none | POST: none") {
    net::Ipv4Header hdr = {};
    hdr.ver_ihl      = 0x45;
    hdr.total_length = __builtin_bswap16(20);
    hdr.ident        = __builtin_bswap16(1);
    hdr.flags_frag   = __builtin_bswap16(0x4000);
    hdr.ttl          = 64;
    hdr.protocol     = 17;  // UDP
    hdr.src = net::Ipv4Addr::from_u32(0xC0A80101);
    hdr.dst = net::Ipv4Addr::from_u32(0xC0A80102);
    hdr.checksum = 0;

    uint16_t sum = net::ipv4_checksum(&hdr);
    hdr.checksum = sum;

    // Verify: checksum of the complete header should be 0
    uint16_t verify = net::ipv4_checksum(&hdr);
    JARVIS_ASSERT_EQ(0, verify);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies that __builtin_bswap16 correctly swaps EtherType
// constants for wire transmission (x86 is little-endian).
// Input: ETH_TYPE_IPV4 = 0x0800, ETH_TYPE_ARP = 0x0806
// Expect: After bswap16, the values are in little-endian for x86 memory
// Depends: __builtin_bswap16
JARVIS_TEST(net_ether_type_swap, "PRE: none | POST: none") {
    uint16_t ipv4_le = __builtin_bswap16(net::ETH_TYPE_IPV4);
    JARVIS_ASSERT_EQ((uint16_t)0x0008, ipv4_le);
    uint16_t arp_le = __builtin_bswap16(net::ETH_TYPE_ARP);
    JARVIS_ASSERT_EQ((uint16_t)0x0608, arp_le);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all network stack tests.
// Input: None
// Expect: All net tests registered
// Depends: Test framework, Logger
void register_net_tests() {
    Logger::info("Registering network stack tests");
    JARVIS_REGISTER_TEST(net_mac_address_ops);
    JARVIS_REGISTER_TEST(net_ipv4_addr_roundtrip);
    JARVIS_REGISTER_TEST(net_arp_cache_ops);
    JARVIS_REGISTER_TEST(net_ipv4_checksum);
    JARVIS_REGISTER_TEST(net_ether_type_swap);
}
