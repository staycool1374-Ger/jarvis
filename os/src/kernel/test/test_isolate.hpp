#pragma once
#include <types.hpp>
#include <kernel/test/resource_tracker.hpp>

namespace kernel::test {

bool snapshot_create();
void snapshot_restore();
void snapshot_destroy();

}
