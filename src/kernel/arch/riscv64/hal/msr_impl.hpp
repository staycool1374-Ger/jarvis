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

/// @file msr_impl.hpp
/// @brief RISC-V64 MSR stubs — RISC-V has no x86-style MSRs; all
///        implementations are no-ops returning 0.

#pragma once

#include <types.hpp>

namespace arch {

/// @brief Write to an x86-style MSR (no-op on RISC-V).
/// @param addr MSR address (ignored).
/// @param val  Value to write (ignored).
inline void wrmsr(uint32_t addr, uint64_t val) { (void)addr; (void)val; }

/// @brief Read from an x86-style MSR (stub on RISC-V).
/// @param addr MSR address (ignored).
/// @return Always 0.
inline uint64_t rdmsr(uint32_t addr) { (void)addr; return 0; }

} // namespace arch
