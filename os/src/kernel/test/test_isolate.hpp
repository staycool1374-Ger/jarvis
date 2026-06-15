#pragma once
#include <types.hpp>
#include <kernel/test/resource_tracker.hpp>

namespace kernel::test {

bool snapshot_create();
void snapshot_restore(const char* test_name = nullptr);
void snapshot_destroy();

/// @brief Terminate old daemon tasks and reload them from initrd.
///        Call AFTER snapshot_restore + snapshot_destroy to replace
///        corrupted page tables with fresh ones from initrd.
void reload_daemon_tasks();

}
