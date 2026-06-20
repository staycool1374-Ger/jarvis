cmake_minimum_required(VERSION 3.20)
project(jarvis-rtos LANGUAGES CXX)

# ============================================================
# CLion IDE Project — Jarvis RTOS (debug mode indexing only)
# ============================================================
# This CMakeLists.txt is for CLion IntelliSense / code navigation.
# Actual builds use `make` from the `os/` directory.
#
# Usage:
#   Open the `clion/` directory in CLion (File → Open).
#   The IDE will index all kernel sources with debug-mode flags.
#
# If CLion shows errors on standard headers (<cstdint>, etc.),
# remove `-target x86_64-elf` below and re-run cmake.
# ============================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Project root (os/ directory) ---
get_filename_component(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)

# --- Debug definitions ---
add_definitions(-DCONFIG_DEBUG)

# --- Compiler flags matching os/Makefile debug target ---
set(JARVIS_CXX_FLAGS
    -std=c++20
    -fno-exceptions
    -fno-rtti
    -fno-builtin
    -fno-stack-protector
    -fno-threadsafe-statics
    -target x86_64-elf
    -m64
    -mno-red-zone
    -mgeneral-regs-only
    -mcmodel=large
    -Wall
    -Wextra
    -Og
    -ffunction-sections
    -fdata-sections
)

# --- Include paths ---
include_directories(
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/src/lib
)

# --- Collect C++ source files ---
file(GLOB_RECURSE CXX_SOURCES
    ${PROJECT_ROOT}/src/kernel/*.cpp
    ${PROJECT_ROOT}/src/lib/*.cpp
    ${PROJECT_ROOT}/src/services/*.cpp
    ${PROJECT_ROOT}/src/programs/*.cpp
    ${PROJECT_ROOT}/src/initrd/*.cpp
)

# --- Static library (compiles .cpp → .o for IDE indexing; no linking) ---
add_library(jarvis-kernel STATIC ${CXX_SOURCES})
target_compile_options(jarvis-kernel PRIVATE ${JARVIS_CXX_FLAGS})

# --- Assembly files (syntax-highlight only in project tree) ---
file(GLOB_RECURSE ASM_SOURCES
    ${PROJECT_ROOT}/src/boot/*.asm
    ${PROJECT_ROOT}/src/kernel/arch/*.asm
    ${PROJECT_ROOT}/src/kernel/syscall/*.asm
    ${PROJECT_ROOT}/src/libc/*.S
)
set_source_files_properties(${ASM_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)
