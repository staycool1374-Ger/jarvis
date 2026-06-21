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
#include <kernel/driver/dma.hpp>
#include <kernel/memory/pmm.hpp>
#include <string.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies DMA buffer allocation returns a valid phys_addr,
// and that the buffer is zero-filled and owned.
// Input: dma::alloc_buffer(512)
// Expect: phys_addr != 0, size >= 512, owned == true
// Depends: PMM, VMM
JARVIS_TEST(dma_alloc_buffer) {
    auto buf = dma::alloc_buffer(512);
    JARVIS_ASSERT(buf.phys_addr != 0);
    JARVIS_ASSERT(buf.size >= 512);
    JARVIS_ASSERT(buf.owned);
    JARVIS_ASSERT(buf.virt_addr != 0);
    // Verify zero-filled
    auto* ptr = reinterpret_cast<volatile uint8_t*>(buf.virt_addr);
    for (size_t i = 0; i < 64; ++i) {
        JARVIS_ASSERT_EQ(0, ptr[i]);
    }
    dma::free_buffer(buf);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies DMA buffer can be written to and read from.
// Input: Allocate buffer, write pattern, read back.
// Expect: Data written matches data read.
// Depends: PMM, VMM
JARVIS_TEST(dma_buffer_write_read) {
    auto buf = dma::alloc_buffer(4096);
    JARVIS_ASSERT(buf.phys_addr != 0);
    auto* ptr = reinterpret_cast<uint8_t*>(buf.virt_addr);
    const char* test_str = "DMA buffer test pattern";
    memcpy(ptr, test_str, 22);
    JARVIS_ASSERT_EQ(0, memcmp(ptr, test_str, 22));
    dma::free_buffer(buf);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies sg_from_buffer creates a single-entry SG list with
// correct phys_addr, virt_addr, and length.
// Input: DMA buffer + sg_from_buffer()
// Expect: SG list count == 1, entries match buffer
// Depends: dma::alloc_buffer
JARVIS_TEST(dma_sg_from_buffer) {
    auto buf = dma::alloc_buffer(2048);
    JARVIS_ASSERT(buf.phys_addr != 0);
    dma::SgList sg;
    dma::sg_reset(sg);
    bool ok = dma::sg_from_buffer(sg, buf);
    JARVIS_ASSERT(ok);
    JARVIS_ASSERT_EQ((size_t)1, sg.count);
    JARVIS_ASSERT_EQ(buf.phys_addr, sg.entries[0].phys_addr);
    JARVIS_ASSERT_EQ(buf.virt_addr, sg.entries[0].virt_addr);
    JARVIS_ASSERT_EQ(buf.size, sg.entries[0].length);
    dma::free_buffer(buf);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies PRD table construction from SG list: byte_count and
// EOT flag are set correctly.
// Input: sg_from_buffer + prd_from_sg
// Expect: PRD count == 1, byte_count == size-1, EOT set
// Depends: dma::alloc_buffer
JARVIS_TEST(dma_prd_from_sg) {
    auto buf = dma::alloc_buffer(4096);
    JARVIS_ASSERT(buf.phys_addr != 0);
    dma::SgList sg;
    dma::sg_reset(sg);
    dma::sg_from_buffer(sg, buf);
    dma::PrdTable prd;
    size_t n = dma::prd_from_sg(prd, sg, true);
    JARVIS_ASSERT_EQ((size_t)1, n);
    JARVIS_ASSERT_EQ(buf.phys_addr, prd.entries[0].phys_addr);
    JARVIS_ASSERT_EQ((uint16_t)(buf.size - 1), prd.entries[0].byte_count);
    JARVIS_ASSERT((prd.entries[0].flags & 0x8000) != 0); // EOT set
    dma::free_buffer(buf);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies pci_set_bus_master on the host bridge (harmless no-op).
// Input: arch::PciBdf for 00:00.0, enable then disable
// Expect: No crash, returns normally
// Depends: arch::pci_config_readw, arch::pci_config_writel
JARVIS_TEST(dma_bus_master_host_bridge) {
    arch::PciBdf bdf = {0, 0, 0};
    // Just verify the code doesn't crash
    dma::pci_set_bus_master(bdf, true);
    dma::pci_set_bus_master(bdf, false);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies sg_reset clears the SG list.
// Input: dma::sg_reset(sg)
// Expect: count == 0, total_length == 0
// Depends: none
JARVIS_TEST(dma_sg_reset) {
    dma::SgList sg;
    sg.count = 5;
    sg.total_length = 1234;
    dma::sg_reset(sg);
    JARVIS_ASSERT_EQ((size_t)0, sg.count);
    JARVIS_ASSERT_EQ((size_t)0, sg.total_length);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies prd_reset clears the PRD table.
// Input: dma::prd_reset(prd)
// Expect: count == 0
// Depends: none
JARVIS_TEST(dma_prd_reset) {
    dma::PrdTable prd;
    prd.count = 10;
    dma::prd_reset(prd);
    JARVIS_ASSERT_EQ((size_t)0, prd.count);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies free_buffer with a zero-initialized buffer is a no-op.
// Input: dma::free_buffer({})
// Expect: No crash
// Depends: none
JARVIS_TEST(dma_free_empty_buffer) {
    dma::DmaBuffer buf = {};
    dma::free_buffer(buf);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies zero-length buffer allocation returns gracefully.
// Input: dma::alloc_buffer(0)
// Expect: Returns DmaBuffer with phys_addr == 0 (failure, not a crash)
// Depends: dma::alloc_buffer
JARVIS_TEST(dma_zero_length_buffer) {
    auto buf = dma::alloc_buffer(0);
    JARVIS_ASSERT_EQ((uint64_t)0, buf.phys_addr);
    JARVIS_ASSERT(!buf.owned);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies PRD table construction from non-contiguous physical pages
// using two separate DMA buffers. Manually builds a 2-entry SG list from
// buffers that are not physically contiguous, then calls prd_from_sg.
// Input: Two dma::alloc_buffer calls, manual SG list setup, prd_from_sg
// Expect: PRD table has 2 entries with different phys_addr ranges
// Depends: dma::alloc_buffer, dma::sg_reset, dma::prd_from_sg
JARVIS_TEST(dma_sg_non_contiguous_prd) {
    auto buf1 = dma::alloc_buffer(4096);
    JARVIS_ASSERT(buf1.phys_addr != 0);
    auto buf2 = dma::alloc_buffer(4096);
    JARVIS_ASSERT(buf2.phys_addr != 0);
    // Buffers should not be contiguous (different allocations)
    JARVIS_ASSERT(buf1.phys_addr != buf2.phys_addr);
    dma::SgList sg;
    dma::sg_reset(sg);
    sg.entries[0].phys_addr = buf1.phys_addr;
    sg.entries[0].virt_addr = buf1.virt_addr;
    sg.entries[0].length    = buf1.size;
    sg.entries[1].phys_addr = buf2.phys_addr;
    sg.entries[1].virt_addr = buf2.virt_addr;
    sg.entries[1].length    = buf2.size;
    sg.count = 2;
    sg.total_length = buf1.size + buf2.size;
    dma::PrdTable prd;
    size_t n = dma::prd_from_sg(prd, sg, true);
    JARVIS_ASSERT_EQ((size_t)2, n);
    JARVIS_ASSERT(prd.entries[0].phys_addr != prd.entries[1].phys_addr);
    JARVIS_ASSERT((prd.entries[1].flags & 0x8000) != 0);
    dma::free_buffer(buf1);
    dma::free_buffer(buf2);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all DMA tests with the test framework.
// Input: None
// Expect: All DMA tests are registered (no direct assertion, only logging)
// Depends: Test framework, Logger, dma
void register_dma_tests() {
    Logger::info("Registering DMA tests");
    JARVIS_REGISTER_TEST(dma_alloc_buffer);
    JARVIS_REGISTER_TEST(dma_buffer_write_read);
    JARVIS_REGISTER_TEST(dma_sg_from_buffer);
    JARVIS_REGISTER_TEST(dma_prd_from_sg);
    JARVIS_REGISTER_TEST(dma_bus_master_host_bridge);
    JARVIS_REGISTER_TEST(dma_sg_reset);
    JARVIS_REGISTER_TEST(dma_prd_reset);
    JARVIS_REGISTER_TEST(dma_free_empty_buffer);
    JARVIS_REGISTER_TEST(dma_zero_length_buffer);
    JARVIS_REGISTER_TEST(dma_sg_non_contiguous_prd);
}
