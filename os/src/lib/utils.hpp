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

/// @file utils.hpp
/// @brief Utility type traits and alignment helpers.

#pragma once

#include <types.hpp>
#include <concepts.hpp>

/// @brief Removes reference qualifiers from a type.
/// @tparam T The type to strip references from.
template<typename T>
struct remove_reference      { using type = T; };
template<typename T>
struct remove_reference<T&>  { using type = T; };
template<typename T>
struct remove_reference<T&&> { using type = T; };

/// @brief Forwards an lvalue argument as an lvalue reference.
/// @tparam T The original type of the argument.
/// @param t  The argument to forward.
/// @return The argument as an rvalue reference to T.
template<typename T>
constexpr T&& forward(typename remove_reference<T>::type& t) noexcept {
    return static_cast<T&&>(t);
}

/// @brief Forwards an rvalue argument as an rvalue reference.
/// @tparam T The original type of the argument.
/// @param t  The argument to forward.
/// @return The argument as an rvalue reference to T.
template<typename T>
constexpr T&& forward(typename remove_reference<T>::type&& t) noexcept {
    return static_cast<T&&>(t);
}

/// @brief Casts an argument to an rvalue reference (move semantics).
/// @tparam T The type of the argument.
/// @param t  The argument to move.
/// @return The argument as an rvalue reference.
template<typename T>
constexpr typename remove_reference<T>::type&& move(T&& t) noexcept {
    return static_cast<typename remove_reference<T>::type&&>(t);
}

/// @brief Returns the address of an object (using compiler builtin).
/// @tparam T The type of the object.
/// @param r  The object reference.
/// @return Pointer to the object.
template<typename T>
constexpr T* address_of(T& r) noexcept {
    return __builtin_addressof(r);
}

/// @brief Compile-time constant wrapper.
/// @tparam T The value type.
/// @tparam v The constant value.
template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
    using value_type = T;
    using type = integral_constant;
    constexpr operator T() const noexcept { return v; }
    constexpr T operator()() const noexcept { return v; }
};

/// @brief Compile-time check for POD types.
/// @tparam T The type to check.
template<typename T>
struct is_pod : integral_constant<bool, __is_pod(T)> {};

/// @brief Compile-time type equality check (false case).
/// @tparam T First type.
/// @tparam U Second type.
template<typename T, typename U>
struct is_same : integral_constant<bool, false> {};
/// @brief Compile-time type equality check (true case).
/// @tparam T The type when both are identical.
template<typename T>
struct is_same<T, T> : integral_constant<bool, true> {};

/// @brief Compile-time check for integral types.
/// @tparam T The type to check.
template<typename T>
struct is_integral : integral_constant<bool,
    is_same<T, bool>::value ||
    is_same<T, char>::value ||
    is_same<T, signed char>::value ||
    is_same<T, unsigned char>::value ||
    is_same<T, short>::value ||
    is_same<T, unsigned short>::value ||
    is_same<T, int>::value ||
    is_same<T, unsigned int>::value ||
    is_same<T, long>::value ||
    is_same<T, unsigned long>::value ||
    is_same<T, long long>::value ||
    is_same<T, unsigned long long>::value
> {};

/// @brief Aligns a value up to the given alignment boundary.
/// @tparam T The value type (must be integral).
/// @param val   The value to align.
/// @param align Alignment boundary (must be power of two).
/// @return The aligned-up value.
template<kernel::Integral T>
static constexpr T align_up(T val, T align) {
    return (val + align - 1) & ~(align - 1);
}

/// @brief Aligns a value down to the given alignment boundary.
/// @tparam T The value type (must be integral).
/// @param val   The value to align.
/// @param align Alignment boundary (must be power of two).
/// @return The aligned-down value.
template<kernel::Integral T>
static constexpr T align_down(T val, T align) {
    return val & ~(align - 1);
}
