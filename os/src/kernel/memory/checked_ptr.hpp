#pragma once

#include <types.hpp>
#include <string.hpp>

namespace kernel {

static constexpr uint64_t USER_SPACE_LIMIT = 0x0000800000000000ULL;

/// @brief Recovery address for safe user memory access.
///        Non-zero while we are inside a user memory copy attempt.
///        Page fault handler checks this and redirects here on fault.
extern "C" uint64_t g_user_access_recover_ip;

static inline bool is_user_range(const void* user_ptr, uint64_t size) {
    if (!user_ptr) return false;
    uint64_t addr = reinterpret_cast<uint64_t>(user_ptr);
    if (addr >= USER_SPACE_LIMIT) return false;
    if (size == 0) return true;
    uint64_t end = addr + size - 1;
    if (end < addr) return false;
    if (end >= USER_SPACE_LIMIT) return false;
    return true;
}

template <typename T>
class CheckedPtr {
    uint64_t addr_;
    uint64_t count_;
public:
    CheckedPtr() : addr_(0), count_(0) {}

    CheckedPtr(T* user_ptr, uint64_t count = 1)
        : addr_(reinterpret_cast<uint64_t>(user_ptr)), count_(count) {}

    bool valid() const {
        if (addr_ == 0) return false;
        return is_user_range(reinterpret_cast<const void*>(addr_),
            count_ * sizeof(T));
    }

    T* unsafe_ptr() const {
        return reinterpret_cast<T*>(addr_);
    }

    bool copy_from(T* kernel_dst) const {
        if (!valid()) return false;
        memcpy(kernel_dst, unsafe_ptr(), count_ * sizeof(T));
        return true;
    }

    bool copy_to(const T* kernel_src) const {
        if (!valid()) return false;
        memcpy(unsafe_ptr(), kernel_src, count_ * sizeof(T));
        return true;
    }

    T read(size_t index = 0) const {
        if (!valid()) return T{};
        return unsafe_ptr()[index];
    }

    bool write(const T& value, size_t index = 0) {
        if (!valid()) return false;
        unsafe_ptr()[index] = value;
        return true;
    }
};

template <typename T>
static inline CheckedPtr<T> checked(T* user_ptr, uint64_t count = 1) {
    return CheckedPtr<T>(user_ptr, count);
}

static inline bool is_user_string(const void* user_ptr, uint64_t max_len = 4096) {
    uint64_t addr = reinterpret_cast<uint64_t>(user_ptr);
    if (addr >= USER_SPACE_LIMIT || addr == 0) return false;
    uint64_t end = addr + max_len - 1;
    if (end < addr || end >= USER_SPACE_LIMIT) return false;
    auto* p = static_cast<const volatile char*>(user_ptr);
    for (uint64_t i = 0; i < max_len; ++i) {
        if (p[i] == '\0') return true;
    }
    return false;
}

static inline bool strncpy_from_user(char* dst, const char* src,
    uint64_t max_len) {
    if (!is_user_string(src, max_len)) return false;
    for (uint64_t i = 0; i < max_len; ++i) {
        dst[i] = src[i];
        if (src[i] == '\0') return true;
    }
    dst[max_len - 1] = '\0';
    return true;
}

/// @brief Safely copies memory from user-space to kernel buffer.
///        Uses fault recovery to handle invalid pointers gracefully.
/// @return true on success, false if a fault or range check failed.
template<typename T>
static inline bool safe_copy_from_user(T* dst, const T* src, uint64_t count) {
    if (!is_user_range(src, count * sizeof(T))) return false;
    g_user_access_recover_ip = reinterpret_cast<uint64_t>(&&recover_from);
    memcpy(dst, src, count * sizeof(T));
    g_user_access_recover_ip = 0;
    return true;

recover_from:
    g_user_access_recover_ip = 0;
    return false;
}

/// @brief Safely copies memory from kernel buffer to user-space.
///        Uses fault recovery to handle invalid pointers gracefully.
/// @return true on success, false if a fault or range check failed.
template<typename T>
static inline bool safe_copy_to_user(T* dst, const T* src, uint64_t count) {
    if (!is_user_range(dst, count * sizeof(T))) return false;
    g_user_access_recover_ip = reinterpret_cast<uint64_t>(&&recover_to);
    memcpy(dst, src, count * sizeof(T));
    g_user_access_recover_ip = 0;
    return true;

recover_to:
    g_user_access_recover_ip = 0;
    return false;
}

} // namespace kernel
