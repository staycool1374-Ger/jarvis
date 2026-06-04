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
    static constexpr size_t MAX_PROGRAMS = 32;
    static const Program* programs_[MAX_PROGRAMS];
    static size_t count_;
};

} // namespace service
