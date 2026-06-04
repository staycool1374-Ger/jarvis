/// @file driver.hpp
/// @brief Driver registry and module management layer.

#pragma once

#include <types.hpp>

namespace kernel {

/// @brief Describes the current state of a driver module.
enum class DriverState : uint8_t {
    UNLOADED,
    LOADED,
    ERROR,
};

/// @brief Describes a single driver module.
struct Driver {
    const char* name;
    const char* description;
    bool (*init)();
    void (*exit)();
    DriverState state;
    uint32_t irq_line;
};

/// @brief Central registry for kernel driver modules.
class DriverRegistry {
public:
    /// @brief Initialises the driver registry.
    static void init();

    /// @brief Registers a driver module.
    static void register_driver(
        const char* name,
        const char* description,
        bool (*init)(),
        void (*exit)(),
        uint32_t irq_line = 0);

    /// @brief Loads and initialises a driver by name.
    /// @return true on success, false if not found or init failed.
    static bool load(const char* name);

    /// @brief Unloads a driver by name.
    static void unload(const char* name);

    /// @brief Looks up a driver by name.
    /// @return Pointer to the Driver, or nullptr.
    static const Driver* find(const char* name);

    /// @brief Returns the number of registered drivers.
    static size_t count() { return count_; }

    /// @brief Returns the driver at the given index.
    static const Driver* get(size_t index) {
        return index < count_ ? drivers_[index] : nullptr;
    }

private:
    static constexpr size_t MAX_DRIVERS = 32;
    static Driver* drivers_[MAX_DRIVERS];
    static size_t count_;
};

} // namespace kernel
