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

namespace kernel {

template<typename T>
concept Integral = __is_integral(T);

template<typename T>
concept TriviallyCopiable = __is_trivially_copyable(T);

template<typename T>
concept ValueType = !__is_reference(T);

template<typename T>
concept ErrorEnum = __is_enum(T);

template<typename T>
concept Lockable = requires(T t) {
    { t.lock() };
    { t.unlock() };
};

} // namespace kernel
