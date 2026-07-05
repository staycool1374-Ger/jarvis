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

/// @file program.hpp
/// @brief Registry of installable user-space programs.

#pragma once

#include <types.hpp>

namespace service {

/// @brief Manages a registry of programs that can be launched by the shell.
class ProgramRegistry {
public:
    /// @brief Describes a registered program.
    struct Program {
        const char* name;
        const char* description;
        void (*entry)();
    };

    /// @brief Initialises the program registry.
    static void init();
    /// @brief Registers a program with the registry.
    /// @param name Name of the program.
    /// @param desc Short description.
    /// @param entry Entry point function.
    static void register_program(const char* name, const char* desc, void (*entry)());
    /// @brief Looks up a program by name.
    /// @param name Program name to find.
    /// @return Pointer to the Program, or nullptr.
    static const Program* find(const char* name);
    /// @brief Returns the number of registered programs.
    /// @return Program count.
    static size_t count() { return count_; }
    /// @brief Returns the program at the given index.
    /// @param index Index into the registry.
    /// @return Pointer to the Program, or nullptr.
    static const Program* get(size_t index) {
        return index < count_ ? programs_[index] : nullptr;
    }

private:
    static constexpr size_t MAX_PROGRAMS = CONFIG_MAX_PROGRAMS;
    // NOLINTNEXTLINE(bugprone-dynamic-static-initializers)
    static const Program* programs_[MAX_PROGRAMS];
    // NOLINTNEXTLINE(bugprone-dynamic-static-initializers)
    static size_t count_;
};

} // namespace service
