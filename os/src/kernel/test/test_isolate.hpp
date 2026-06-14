#pragma once
#include <types.hpp>
#include <kernel/test/resource_tracker.hpp>

namespace kernel::test {

bool snapshot_create();
void snapshot_restore(const char* test_name = nullptr);
void snapshot_destroy();

}
