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

#pragma once

/// @brief Default deleter — calls `delete` on the owned pointer.
struct DefaultDeleter {
    template<typename T>
    void operator()(T* p) const { delete p; }
};

/// @brief Minimal unique ownership wrapper for heap-allocated objects.
///        Deletes the owned object on destruction using Deleter. Move-only.
template <typename T, typename Deleter = DefaultDeleter>
class UniquePtr {
    T* ptr_;
    [[no_unique_address]] Deleter del_;
public:
    explicit UniquePtr(T* p = nullptr) : ptr_(p) {}
    UniquePtr(T* p, Deleter d) : ptr_(p), del_(d) {}
    ~UniquePtr() { if (ptr_) del_(ptr_); }

    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;

    UniquePtr(UniquePtr&& other) noexcept : ptr_(other.ptr_), del_(other.del_) {
        other.ptr_ = nullptr;
    }
    UniquePtr& operator=(UniquePtr&& other) noexcept {
        if (this != &other) {
            if (ptr_) del_(ptr_);
            ptr_ = other.ptr_;
            del_ = other.del_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    T* get() const { return ptr_; }
    Deleter& get_deleter() { return del_; }
    const Deleter& get_deleter() const { return del_; }
    T* release() { T* p = ptr_; ptr_ = nullptr; return p; }
    void reset(T* p = nullptr) { if (ptr_) del_(ptr_); ptr_ = p; }

    T& operator*() const { return *ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

    friend bool operator==(const UniquePtr& a, decltype(nullptr)) { return a.ptr_ == nullptr; }
    friend bool operator==(decltype(nullptr), const UniquePtr& a) { return a.ptr_ == nullptr; }
    friend bool operator!=(const UniquePtr& a, decltype(nullptr)) { return a.ptr_ != nullptr; }
    friend bool operator!=(decltype(nullptr), const UniquePtr& a) { return a.ptr_ != nullptr; }
};
