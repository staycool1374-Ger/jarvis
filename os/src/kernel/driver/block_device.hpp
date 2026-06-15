#pragma once

#include <types.hpp>

namespace kernel {
namespace block {

static constexpr uint64_t BLOCK_SIZE = 512;

class BlockDevice {
public:
    virtual ~BlockDevice() = default;

    virtual bool read_sector(uint64_t lba, uint8_t* buffer) = 0;
    virtual bool write_sector(uint64_t lba, const uint8_t* buffer) = 0;
    virtual uint64_t sector_count() const = 0;
    virtual uint64_t sector_size() const = 0;
    virtual bool is_read_only() const = 0;
};

class MockBlockDevice final : public BlockDevice {
public:
    /// @brief Allocate writable storage via PMM.
    MockBlockDevice(uint64_t sector_count_);
    /// @brief Wrap an existing buffer (may be read-only).
    MockBlockDevice(const uint8_t* external_data, uint64_t sector_count_,
                    bool read_only);
    ~MockBlockDevice();

    bool read_sector(uint64_t lba, uint8_t* buffer) override;
    bool write_sector(uint64_t lba, const uint8_t* buffer) override;
    uint64_t sector_count() const override { return sector_count_; }
    uint64_t sector_size() const override { return BLOCK_SIZE; }
    bool is_read_only() const override { return read_only_; }

    uint8_t* raw_buffer() { return owns_data_ ? data_ : nullptr; }

private:
    uint64_t sector_count_;
    uint8_t* data_;
    bool read_only_ = false;
    bool owns_data_ = true;
};

} // namespace block
} // namespace kernel
