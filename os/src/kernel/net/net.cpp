/// @file net.cpp
/// @brief Network stack core implementation.

#include <kernel/net/net.hpp>
#include <string.hpp>
#include <logger.hpp>

using namespace kernel;

namespace net {

static ArpCache g_arp_cache;
static uint16_t g_ip_ident = 0;

ArpCache& net_arp_cache() { return g_arp_cache; }

void net_init(Nic& nic, MacAddr mac, Ipv4Addr ip, Ipv4Addr subnet, Ipv4Addr gateway) {
    nic.mac     = mac;
    nic.ip      = ip;
    nic.subnet  = subnet;
    nic.gateway = gateway;
    g_arp_cache.clear();
    Logger::info("net: initialized %d.%d.%d.%d",
        ip.addr[0], ip.addr[1], ip.addr[2], ip.addr[3]);
}

void net_handle_frame(const uint8_t* data, size_t len, Nic& nic) {
    if (len < sizeof(EtherHeader)) return;
    auto* eth = reinterpret_cast<const EtherHeader*>(data);
    uint16_t type = __builtin_bswap16(eth->type);

    if (type == ETH_TYPE_ARP) {
        if (len < sizeof(EtherHeader) + sizeof(ArpHeader)) return;
        auto* arp = reinterpret_cast<const ArpHeader*>(data + sizeof(EtherHeader));

        if (arp->htype != __builtin_bswap16(ARP_HTYPE_ETHER)) return;
        if (arp->ptype != __builtin_bswap16(ETH_TYPE_IPV4)) return;

        uint16_t oper = __builtin_bswap16(arp->oper);

        if (oper == ARP_OPER_REQUEST) {
            // Check if the target IP is ours
            if (arp->tpa == nic.ip.as_u32()) {
                // Send ARP reply
                uint8_t reply_buf[sizeof(EtherHeader) + sizeof(ArpHeader)];
                auto* reply_eth = reinterpret_cast<EtherHeader*>(reply_buf);
                auto* reply_arp = reinterpret_cast<ArpHeader*>(reply_buf + sizeof(EtherHeader));

                reply_eth->dst = eth->src;
                reply_eth->src = nic.mac;
                reply_eth->type = __builtin_bswap16(ETH_TYPE_ARP);

                reply_arp->htype = arp->htype;
                reply_arp->ptype = arp->ptype;
                reply_arp->hlen  = ETH_ADDR_LEN;
                reply_arp->plen  = IPV4_ADDR_LEN;
                reply_arp->oper  = __builtin_bswap16(ARP_OPER_REPLY);
                reply_arp->sha   = nic.mac;
                reply_arp->spa   = arp->tpa;
                reply_arp->tha   = arp->sha;
                reply_arp->tpa   = arp->spa;

                nic.send_frame(reply_buf, sizeof(reply_buf));
            }
        } else if (oper == ARP_OPER_REPLY) {
            // Update ARP cache
            g_arp_cache.update(arp->spa, arp->sha);
        }
    } else if (type == ETH_TYPE_IPV4) {
        if (len < sizeof(EtherHeader) + sizeof(Ipv4Header)) return;
        // IPv4 packet received — could dispatch to UDP/TCP/ICMP handlers
        // For now, we just log and ignore
        Logger::info("net: IPv4 packet from %d.%d.%d.%d proto=%d",
            eth->src.addr[0], eth->src.addr[1], eth->src.addr[2], eth->src.addr[3],
            reinterpret_cast<const Ipv4Header*>(data + sizeof(EtherHeader))->protocol);
    }
}

bool net_arp_resolve(Nic& nic, uint32_t target_ip, MacAddr& out_mac) {
    // Check cache first
    if (g_arp_cache.lookup(target_ip, out_mac)) return true;

    // Send ARP request
    uint8_t buf[sizeof(EtherHeader) + sizeof(ArpHeader)];
    auto* eth = reinterpret_cast<EtherHeader*>(buf);
    auto* arp = reinterpret_cast<ArpHeader*>(buf + sizeof(EtherHeader));

    eth->dst  = MAC_BROADCAST;
    eth->src  = nic.mac;
    eth->type = __builtin_bswap16(ETH_TYPE_ARP);

    arp->htype = __builtin_bswap16(ARP_HTYPE_ETHER);
    arp->ptype = __builtin_bswap16(ETH_TYPE_IPV4);
    arp->hlen  = ETH_ADDR_LEN;
    arp->plen  = IPV4_ADDR_LEN;
    arp->oper  = __builtin_bswap16(ARP_OPER_REQUEST);
    arp->sha   = nic.mac;
    arp->spa   = nic.ip.as_u32();
    arp->tha   = MAC_NULL;
    arp->tpa   = target_ip;

    nic.send_frame(buf, sizeof(buf));
    return true;
}

bool net_send_udp(Nic& nic, Ipv4Addr dst_ip, uint16_t dst_port,
                  uint16_t src_port, const uint8_t* payload, size_t payload_len) {
    // Resolve MAC via ARP
    MacAddr dst_mac;
    if (!net_arp_resolve(nic, dst_ip.as_u32(), dst_mac)) {
        Logger::warn("net: ARP resolution failed for %d.%d.%d.%d",
            dst_ip.addr[0], dst_ip.addr[1], dst_ip.addr[2], dst_ip.addr[3]);
        return false;
    }

    size_t ip_payload_len = sizeof(UdpHeader) + payload_len;
    size_t total_ip_len = IPV4_MIN_HEADER_LEN + ip_payload_len;
    size_t frame_len = sizeof(EtherHeader) + total_ip_len;

    if (frame_len > MAX_PACKET_SIZE) {
        Logger::error("net: packet too large (%zu)", frame_len);
        return false;
    }

    uint8_t buf[MAX_PACKET_SIZE];
    memset(buf, 0, MAX_PACKET_SIZE);

    // Ethernet header
    auto* eth = reinterpret_cast<EtherHeader*>(buf);
    eth->dst  = dst_mac;
    eth->src  = nic.mac;
    eth->type = __builtin_bswap16(ETH_TYPE_IPV4);

    // IPv4 header
    auto* ip = reinterpret_cast<Ipv4Header*>(buf + sizeof(EtherHeader));
    ip->ver_ihl       = 0x45;        // IPv4, 20-byte header
    ip->dscp_ecn      = 0;
    ip->total_length  = __builtin_bswap16(static_cast<uint16_t>(total_ip_len));
    ip->ident         = __builtin_bswap16(g_ip_ident++);
    ip->flags_frag    = __builtin_bswap16(0x4000); // Don't Fragment
    ip->ttl           = 64;
    ip->protocol      = IP_PROTO_UDP;
    ip->checksum      = 0;
    ip->src           = nic.ip;
    ip->dst           = dst_ip;
    ip->checksum      = ipv4_checksum(ip);

    // UDP header
    auto* udp = reinterpret_cast<UdpHeader*>(buf + sizeof(EtherHeader) + IPV4_MIN_HEADER_LEN);
    udp->src_port = __builtin_bswap16(src_port);
    udp->dst_port = __builtin_bswap16(dst_port);
    udp->length   = __builtin_bswap16(static_cast<uint16_t>(sizeof(UdpHeader) + payload_len));
    udp->checksum = 0;  // UDP checksum is optional (0 = no checksum)

    // Payload
    if (payload_len > 0) {
        memcpy(buf + sizeof(EtherHeader) + IPV4_MIN_HEADER_LEN + sizeof(UdpHeader),
               payload, payload_len);
    }

    return nic.send_frame(buf, frame_len);
}

} // namespace net
