/// @file net.hpp
/// @brief Network stack core — NIC interface, packet dispatch.

#pragma once

#include <types.hpp>
#include <kernel/net/ether.hpp>
#include <kernel/net/arp.hpp>
#include <kernel/net/ipv4.hpp>
#include <kernel/net/udp.hpp>
#include <kernel/net/icmp.hpp>

namespace net {

constexpr size_t MAX_PACKET_SIZE = 2048;

/// NIC (Network Interface Card) abstraction
struct Nic {
    MacAddr mac;         // this NIC's MAC address
    Ipv4Addr ip;         // assigned IPv4 address
    Ipv4Addr subnet;     // subnet mask
    Ipv4Addr gateway;    // default gateway

    /// Send a raw Ethernet frame. Implemented by the NIC driver.
    bool (*send_frame)(const uint8_t* data, size_t len);

    /// Poll for a received frame. Non-blocking.
    bool (*poll_frame)(uint8_t* buf, size_t& len);

    /// Called when a frame arrives — set by the stack.
    void (*on_frame)(const uint8_t* data, size_t len);

    /// Driver private data
    void* driver_data;
};

/// Initialize the network stack for a NIC.
void net_init(Nic& nic, MacAddr mac, Ipv4Addr ip, Ipv4Addr subnet, Ipv4Addr gateway);

/// Handle an incoming Ethernet frame (called by the NIC driver).
void net_handle_frame(const uint8_t* data, size_t len, Nic& nic);

/// Send an ARP request to resolve @p target_ip.
bool net_arp_resolve(Nic& nic, uint32_t target_ip, MacAddr& out_mac);

/// Send an IPv4 + UDP packet.
/// @return true if the packet was enqueued for transmission.
bool net_send_udp(Nic& nic, Ipv4Addr dst_ip, uint16_t dst_port,
                  uint16_t src_port, const uint8_t* payload, size_t payload_len);

/// Get the ARP cache (for inspection/debugging).
ArpCache& net_arp_cache();

/// Send an ICMP Echo Request.
/// @return true if the request was sent.
bool net_send_icmp_echo(Nic& nic, Ipv4Addr dst_ip, uint16_t id, uint16_t seq,
                        const uint8_t* data, size_t data_len);

/// Globally registered NIC (set by the first call to net_init).
extern Nic* g_nic;

/// ICMP echo reply record for ping.
struct IcmpEchoReply {
    bool     received;
    uint16_t ident;
    uint16_t seq;
    uint64_t rx_tick;
    Ipv4Addr src;
};

/// Reset the ICMP echo reply record.
void net_icmp_clear_reply();
/// Get the last ICMP echo reply received.
const IcmpEchoReply* net_icmp_last_reply();

/// Poll the NIC for an incoming frame and dispatch it.
/// Non-blocking — returns false if nothing available.
bool net_poll(Nic& nic);

} // namespace net
