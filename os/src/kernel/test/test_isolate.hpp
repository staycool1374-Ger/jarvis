#pragma once
#include <types.hpp>

namespace kernel::test {

bool snapshot_create();
void snapshot_restore();
void snapshot_destroy();

}
