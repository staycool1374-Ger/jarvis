#include <kernel/memory/pmm.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <constants.hpp>
#include <utils.hpp>
#include <assert.hpp>
#include <kernel/memory/pmm_errors.hpp>

namespace kernel {

uint64_t PMM::total_pages_ = 0;
uint64_t PMM::free_pages_ = 0;
uint64_t PMM::bitmap_ = 0;
uint64_t PMM::bitmap_size_ = 0;
uint64_t PMM::owner_bitmap_ = 0;
uint64_t PMM::page_table_pool_start_ = 0;
uint64_t PMM::page_table_pool_end_ = 0;
PMM::OOMHandler PMM::oom_handler_ = nullptr;

void PMM::init(uint64_t mem_size, uint64_t kernel_start, uint64_t kernel_end) {
    total_pages_ = mem_size / PAGE_SIZE;
    free_pages_ = total_pages_;

    bitmap_size_ = align_up<uint64_t>(total_pages_ / 8, 8_KiB);
    uint64_t bitmap_phys = align_up<uint64_t>(kernel_end, 8_KiB);
    uint64_t owner_bitmap_phys = bitmap_phys + bitmap_size_;
    bitmap_ = arch::HHDM_OFFSET + bitmap_phys;
    owner_bitmap_ = arch::HHDM_OFFSET + owner_bitmap_phys;

    auto* bitmap = reinterpret_cast<uint8_t*>(bitmap_);
    auto* owner = reinterpret_cast<uint8_t*>(owner_bitmap_);
    for (uint64_t i = 0; i < bitmap_size_; ++i) {
        bitmap[i] = 0;
        owner[i] = 0;
    }

    uint64_t kernel_start_page = kernel_start / PAGE_SIZE;
    uint64_t reserved_end = bitmap_phys + bitmap_size_ + bitmap_size_;
    uint64_t reserved_end_page = (reserved_end + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint64_t i = 0; i < kernel_start_page; ++i) {
        bitmap_set(i);
        owner_set_kernel(i);
        --free_pages_;
    }

    for (uint64_t i = kernel_start_page; i < reserved_end_page; ++i) {
        bitmap_set(i);
        owner_set_kernel(i);
        --free_pages_;
    }

    uint64_t pool_start_page = reserved_end_page;
    uint64_t pool_size_pages = 4096;
    if (pool_start_page + pool_size_pages < total_pages_) {
        page_table_pool_start_ = pool_start_page * PAGE_SIZE;
        page_table_pool_end_ = page_table_pool_start_ +
                               pool_size_pages * PAGE_SIZE;
        for (uint64_t i = pool_start_page;
             i < pool_start_page + pool_size_pages; ++i) {
            bitmap_set(i);
            owner_set_kernel(i);
            --free_pages_;
        }
    }

    if (total_pages_ > 16) {
        for (uint64_t i = total_pages_ - 1; i >= total_pages_ - 16; --i) {
            if (!bitmap_test(i)) {
                bitmap_set(i);
                owner_set_kernel(i);
                --free_pages_;
            }
        }
    }
}

uint64_t PMM::try_alloc_kernel(size_t count) {
    for (uint64_t i = 0; i <= total_pages_ - count; ++i) {
        bool ok = true;
        for (size_t j = 0; j < count; ++j) {
            if (bitmap_test(i + j)) { ok = false; break; }
        }
        if (ok) {
            for (size_t j = 0; j < count; ++j) {
                bitmap_set(i + j);
                owner_set_kernel(i + j);
                --free_pages_;
            }
            return i * PAGE_SIZE;
        }
    }
    return 0;
}

uint64_t PMM::try_alloc_user(size_t count) {
    for (uint64_t i = 0; i <= total_pages_ - count; ++i) {
        bool ok = true;
        for (size_t j = 0; j < count; ++j) {
            if (bitmap_test(i + j)) { ok = false; break; }
        }
        if (ok) {
            for (size_t j = 0; j < count; ++j) {
                bitmap_set(i + j);
                owner_set_user(i + j);
                --free_pages_;
            }
            return i * PAGE_SIZE;
        }
    }
    return 0;
}

uint64_t PMM::alloc_page() {
    uint64_t result = try_alloc_kernel(1);
    if (result) return result;
    if (oom_handler_ && oom_handler_()) {
        result = try_alloc_kernel(1);
    }
    if (!result) { ASSERT(errors::PmmError::PMM_ERR_OOM); }
    return result;
}

uint64_t PMM::alloc_contiguous(size_t count) {
    if (count == 0 || count > total_pages_) return 0;
    uint64_t result = try_alloc_kernel(count);
    if (result) return result;
    if (oom_handler_ && oom_handler_()) {
        result = try_alloc_kernel(count);
    }
    if (!result) { ASSERT(errors::PmmError::PMM_ERR_OOM); }
    return result;
}

uint64_t PMM::alloc_user_page() {
    uint64_t result = try_alloc_user(1);
    if (result) return result;
    if (oom_handler_ && oom_handler_()) {
        result = try_alloc_user(1);
    }
    if (!result) { ASSERT(errors::PmmError::PMM_ERR_USER_OOM); }
    return result;
}

uint64_t PMM::alloc_user_contiguous(size_t count) {
    if (count == 0 || count > total_pages_) return 0;
    uint64_t result = try_alloc_user(count);
    if (result) return result;
    if (oom_handler_ && oom_handler_()) {
        result = try_alloc_user(count);
    }
    if (!result) { ASSERT(errors::PmmError::PMM_ERR_USER_OOM); }
    return result;
}

uint64_t PMM::alloc_page_table() {
    if (page_table_pool_start_ == 0 || page_table_pool_end_ == 0) {
        return alloc_page();
    }
    uint64_t pool_start_page = page_table_pool_start_ / PAGE_SIZE;
    uint64_t pool_end_page = page_table_pool_end_ / PAGE_SIZE;
    for (uint64_t i = pool_start_page; i < pool_end_page; ++i) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            owner_set_kernel(i);
            --free_pages_;
            return i * PAGE_SIZE;
        }
    }
    uint64_t result = alloc_page();
    if (!result) { ASSERT(errors::PmmError::PMM_ERR_TABLE_OOM); }
    return result;
}

void PMM::free_page(uint64_t phys_addr) {
    uint64_t index = phys_addr / PAGE_SIZE;
    if (index >= total_pages_) return;
    if (bitmap_test(index)) {
        bitmap_clear(index);
        ++free_pages_;
    }
}

bool PMM::is_user_page(uint64_t phys_addr) {
    uint64_t index = phys_addr / arch::PAGE_SIZE;
    if (index >= total_pages_) return false;
    return owner_test(index);
}

void PMM::bitmap_set(size_t index) {
    ENSURE(index < total_pages_);
    auto* bitmap = reinterpret_cast<uint8_t*>(bitmap_);
    bitmap[index / 8] |= static_cast<uint8_t>(1 << (index % 8));
}

void PMM::bitmap_clear(size_t index) {
    ENSURE(index < total_pages_);
    auto* bitmap = reinterpret_cast<uint8_t*>(bitmap_);
    bitmap[index / 8] &= static_cast<uint8_t>(~(1 << (index % 8)));
}

bool PMM::bitmap_test(size_t index) {
    ENSURE(index < total_pages_);
    auto* bitmap = reinterpret_cast<uint8_t*>(bitmap_);
    return (bitmap[index / 8] >> (index % 8)) & 1;
}

void PMM::owner_set_user(size_t index) {
    ENSURE(index < total_pages_);
    auto* owner = reinterpret_cast<uint8_t*>(owner_bitmap_);
    owner[index / 8] |= static_cast<uint8_t>(1 << (index % 8));
}

void PMM::owner_set_kernel(size_t index) {
    ENSURE(index < total_pages_);
    auto* owner = reinterpret_cast<uint8_t*>(owner_bitmap_);
    owner[index / 8] &= static_cast<uint8_t>(~(1 << (index % 8)));
}

bool PMM::owner_test(size_t index) {
    ENSURE(index < total_pages_);
    auto* owner = reinterpret_cast<uint8_t*>(owner_bitmap_);
    return (owner[index / 8] >> (index % 8)) & 1;
}

} // namespace kernel
