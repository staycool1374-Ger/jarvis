/// @file virtio_net.cpp
/// @brief Virtio-net NIC driver implementation.

#include <kernel/driver/virtio_net.hpp>
#include <kernel/memory/pmm.hpp>
#include <string.hpp>
#include <logger.hpp>

using namespace kernel;
using namespace arch;

namespace kernel::net {

static bool virtio_net_send_frame(const uint8_t* data, size_t len);

VirtioNetDevice* g_virtio_net_dev = nullptr;

static bool alloc_queue_pages(uint64_t& desc_phys, uint64_t& avail_phys,
                               uint64_t& used_phys,
                               arch::VirtqDesc*& desc,
                               arch::VirtqAvail*& avail,
                               arch::VirtqUsed*& used,
                               uint16_t queue_size) {
    size_t desc_size  = queue_size * sizeof(arch::VirtqDesc);
    size_t avail_size = sizeof(uint16_t) * 2 + queue_size * sizeof(uint16_t);
    size_t used_size  = sizeof(uint16_t) * 2 + queue_size * sizeof(arch::VirtqUsedElem);

    // Round to pages
    size_t desc_pages  = (desc_size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t avail_pages = (avail_size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t used_pages  = (used_size + PAGE_SIZE - 1) / PAGE_SIZE;

    desc_phys  = PMM::alloc_contiguous(desc_pages);
    avail_phys = PMM::alloc_contiguous(avail_pages);
    used_phys  = PMM::alloc_contiguous(used_pages);

    if (!desc_phys || !avail_phys || !used_phys) {
        Logger::error("virtio-net: OOM for queue pages");
        return false;
    }

    desc  = reinterpret_cast<arch::VirtqDesc*>(HHDM_OFFSET + desc_phys);
    avail = reinterpret_cast<arch::VirtqAvail*>(HHDM_OFFSET + avail_phys);
    used  = reinterpret_cast<arch::VirtqUsed*>(HHDM_OFFSET + used_phys);

    memset(desc, 0, desc_pages * PAGE_SIZE);
    memset(avail, 0, avail_pages * PAGE_SIZE);
    memset(used, 0, used_pages * PAGE_SIZE);

    return true;
}

static void add_rx_buf(VirtioNetDevice& dev, int idx) {
    uint16_t qi = static_cast<uint16_t>(idx);
    dev.rx_desc[qi].addr  = dev.rx_bufs_phys[idx];
    dev.rx_desc[qi].len   = MAX_PACKET_SIZE;
    dev.rx_desc[qi].flags = VIRTIO_DESC_F_WRITE;
    dev.rx_desc[qi].next  = 0;

    dev.rx_avail->ring[dev.rx_avail->idx % dev.queue_size] = qi;
    __sync_synchronize();
    dev.rx_avail->idx = static_cast<uint16_t>(dev.rx_avail->idx + 1);
}

bool virtio_net_probe(Nic& nic) {
    arch::VirtioTransport transport;
    if (!arch::virtio_find_device(VIRTIO_DEVICE_NET, transport)) {
        // Try legacy
        if (!arch::virtio_find_device(VIRTIO_DEVICE_NET_LEGACY, transport)) {
            Logger::info("virtio-net: no device found");
            return false;
        }
    }

    if (!arch::virtio_init_transport(transport)) {
        Logger::error("virtio-net: transport init failed");
        return false;
    }

    uint64_t features = VIRTIO_F_VERSION_1;
    if (!arch::virtio_negotiate_features(transport, features)) {
        return false;
    }

    auto* dev = new VirtioNetDevice();
    dev->transport = transport;
    dev->queue_size = 16;
    dev->rx_avail_idx = 0;
    dev->tx_avail_idx = 0;

    // Allocate queue memory
    if (!alloc_queue_pages(dev->rx_desc_phys, dev->rx_avail_phys, dev->rx_used_phys,
                           dev->rx_desc, dev->rx_avail, dev->rx_used, dev->queue_size) ||
        !alloc_queue_pages(dev->tx_desc_phys, dev->tx_avail_phys, dev->tx_used_phys,
                           dev->tx_desc, dev->tx_avail, dev->tx_used, dev->queue_size)) {
        delete dev;
        return false;
    }

    // Setup RX and TX queues
    if (!arch::virtio_setup_queue(transport, VIRTIO_NET_QUEUE_RX, dev->queue_size,
                                   dev->rx_desc_phys, dev->rx_avail_phys, dev->rx_used_phys) ||
        !arch::virtio_setup_queue(transport, VIRTIO_NET_QUEUE_TX, dev->queue_size,
                                   dev->tx_desc_phys, dev->tx_avail_phys, dev->tx_used_phys)) {
        delete dev;
        return false;
    }

    // Allocate RX DMA buffers
    for (int i = 0; i < 16; ++i) {
        uint64_t phys = PMM::alloc_page();
        if (!phys) {
            Logger::error("virtio-net: OOM for RX buffer %d", i);
            delete dev;
            return false;
        }
        dev->rx_bufs_phys[i] = phys;
        dev->rx_bufs[i] = reinterpret_cast<uint8_t*>(HHDM_OFFSET + phys);
        memset(dev->rx_bufs[i], 0, PAGE_SIZE);

        // Add to RX available ring
        add_rx_buf(*dev, i);
    }

    // Allocate TX DMA buffer
    dev->tx_buf_phys = PMM::alloc_page();
    if (!dev->tx_buf_phys) {
        delete dev;
        return false;
    }
    dev->tx_buf = reinterpret_cast<uint8_t*>(HHDM_OFFSET + dev->tx_buf_phys);

    // DRIVER_OK
    uint8_t status = arch::virtio_read_status(transport);
    arch::virtio_write_status(transport, status | VIRTIO_STATUS_DRIVER_OK);

    // Read MAC from device config (offset 0 for virtio-net)
    auto* cfg = reinterpret_cast<volatile uint8_t*>(transport.device_cfg.virt_addr);
    MacAddr mac;
    for (int i = 0; i < 6; ++i) mac.addr[i] = cfg[i];

    // Initialize NIC abstraction
    nic.mac  = mac;
    nic.ip   = Ipv4Addr::from_u32(0); // assigned later
    nic.subnet = Ipv4Addr::from_u32(0);
    nic.gateway = Ipv4Addr::from_u32(0);
    nic.send_frame = virtio_net_send_frame;
    nic.driver_data = dev;

    dev->nic = &nic;
    g_virtio_net_dev = dev;

    Logger::info("virtio-net: MAC %02x:%02x:%02x:%02x:%02x:%02x",
        mac.addr[0], mac.addr[1], mac.addr[2],
        mac.addr[3], mac.addr[4], mac.addr[5]);
    return true;
}

static bool virtio_net_send_frame(const uint8_t* data, size_t len) {
    if (!g_virtio_net_dev) return false;
    auto& dev = *g_virtio_net_dev;

    if (len + VIRTIO_NET_HDR_SIZE > PAGE_SIZE) return false;

    // Copy data into the TX buffer with virtio-net header prepended
    auto* hdr = reinterpret_cast<VirtioNetHdr*>(dev.tx_buf);
    memset(hdr, 0, VIRTIO_NET_HDR_SIZE);
    memcpy(dev.tx_buf + VIRTIO_NET_HDR_SIZE, data, len);

    uint16_t idx = dev.tx_avail_idx % dev.queue_size;
    dev.tx_desc[idx].addr  = dev.tx_buf_phys;
    dev.tx_desc[idx].len   = static_cast<uint32_t>(VIRTIO_NET_HDR_SIZE + len);
    dev.tx_desc[idx].flags = 0;
    dev.tx_desc[idx].next  = 0;

    dev.tx_avail->ring[dev.tx_avail->idx % dev.queue_size] = idx;
    __sync_synchronize();
    dev.tx_avail->idx = static_cast<uint16_t>(dev.tx_avail->idx + 1);
    __sync_synchronize();

    arch::virtio_notify(dev.transport, VIRTIO_NET_QUEUE_TX);

    // Poll for completion
    uint16_t used_idx = dev.tx_used->idx;
    int timeout = 100000;
    while (dev.tx_used->idx == used_idx && --timeout > 0) {
        __sync_synchronize();
    }

    dev.tx_avail_idx = static_cast<uint16_t>(dev.tx_avail_idx + 1);
    return timeout > 0;
}

} // namespace kernel::net
