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
