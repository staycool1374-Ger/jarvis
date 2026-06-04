#include <kernel/memory/pmm.hpp>
#include <utils.hpp>
#include <assert.hpp>

namespace kernel {

uint64_t PMM::total_pages_ = 0;
uint64_t PMM::free_pages_ = 0;
uint64_t PMM::bitmap_ = 0;
uint64_t PMM::bitmap_size_ = 0;

void PMM::init(uint64_t mem_size, uint64_t kernel_start, uint64_t kernel_end) {
    total_pages_ = mem_size / PAGE_SIZE;
    free_pages_ = total_pages_;

    bitmap_size_ = align_up<uint64_t>(total_pages_ / 8, 8_KiB);
    bitmap_ = align_up<uint64_t>(kernel_end, 8_KiB);

    auto* bitmap = reinterpret_cast<uint8_t*>(bitmap_);
    for (uint64_t i = 0; i < bitmap_size_; ++i) {
        bitmap[i] = 0;
    }

    uint64_t kernel_start_page = kernel_start / PAGE_SIZE;
    uint64_t reserved_end = bitmap_ + bitmap_size_;
    uint64_t reserved_end_page = (reserved_end + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint64_t i = 0; i < kernel_start_page; ++i) {
        bitmap_set(i);
        --free_pages_;
    }

    for (uint64_t i = kernel_start_page; i < reserved_end_page; ++i) {
        bitmap_set(i);
        --free_pages_;
    }

    if (total_pages_ > 16) {
        for (uint64_t i = total_pages_ - 1; i >= total_pages_ - 16; --i) {
            if (!bitmap_test(i)) {
                bitmap_set(i);
                --free_pages_;
            }
        }
    }
}

uint64_t PMM::alloc_page() {
    for (uint64_t i = 0; i < total_pages_; ++i) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            --free_pages_;
            return i * PAGE_SIZE;
        }
    }
    return 0;
}

uint64_t PMM::alloc_contiguous(size_t count) {
    if (count == 0 || count > total_pages_) return 0;
    for (uint64_t i = 0; i <= total_pages_ - count; ++i) {
        bool ok = true;
        for (size_t j = 0; j < count; ++j) {
            if (bitmap_test(i + j)) { ok = false; break; }
        }
        if (ok) {
            for (size_t j = 0; j < count; ++j) {
                bitmap_set(i + j);
                --free_pages_;
            }
            return i * PAGE_SIZE;
        }
    }
    return 0;
}

void PMM::free_page(uint64_t phys_addr) {
    uint64_t index = phys_addr / PAGE_SIZE;
    if (index >= total_pages_) return;
    if (bitmap_test(index)) {
        bitmap_clear(index);
        ++free_pages_;
    }
}

void PMM::bitmap_set(size_t index) {
    ASSERT(index < total_pages_);
    auto* bitmap = reinterpret_cast<uint8_t*>(bitmap_);
    bitmap[index / 8] |= static_cast<uint8_t>(1 << (index % 8));
}

void PMM::bitmap_clear(size_t index) {
    ASSERT(index < total_pages_);
    auto* bitmap = reinterpret_cast<uint8_t*>(bitmap_);
    bitmap[index / 8] &= static_cast<uint8_t>(~(1 << (index % 8)));
}

bool PMM::bitmap_test(size_t index) {
    ASSERT(index < total_pages_);
    auto* bitmap = reinterpret_cast<uint8_t*>(bitmap_);
    return (bitmap[index / 8] >> (index % 8)) & 1;
}

} // namespace kernel
