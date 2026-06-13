#pragma once

#include <kernel/sync/semaphore.hpp>
#include <kernel/sync/mutex.hpp>
#include <kernel/sync/queue.hpp>
#include <kernel/sync/notify.hpp>
#include <kernel/sync/eventgroup.hpp>

namespace kernel {
namespace sync {

/// @brief Initialize all kernel synchronization primitives.
void init_all();

}
}
