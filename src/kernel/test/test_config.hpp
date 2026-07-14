#pragma once

/// @file test_config.hpp
/// @brief Test configuration constants.

#include <types.hpp>

namespace kernel::test {

/// @brief Parse test configuration from initrd file
/// @returns Vector of class names to run, or {"safe"} if file not found/invalid
void parse_test_config(const char *path);

/// @brief Get the list of test classes to run (populated by parse_test_config)
/// @returns Null-terminated array of class names
const char **get_test_classes();

/// @brief Get the count of test classes to run
size_t get_test_class_count();

/// @brief Check if a specific test class should run
bool should_run_class(const char *class_name);

} // namespace kernel::test