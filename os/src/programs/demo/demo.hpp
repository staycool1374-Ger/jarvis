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

/// @file demo.hpp
/// @brief Demo programs: Mandelbrot renderer and CPU benchmark.

#pragma once

#include <types.hpp>

namespace programs {

/// @brief Entry point for the demo / Mandelbrot program.
void demo_main();
/// @brief Draws the Mandelbrot set on the framebuffer.
void draw_mandelbrot();
/// @brief Runs a simple CPU performance benchmark.
/// @return Benchmark score (arbitrary units).
uint64_t bench_cpu();

} // namespace programs
