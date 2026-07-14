#pragma once

/// @file   task_ptr.hpp
/// @brief  UniquePtr wrappers for TaskControlBlock lifetime in tests.

#include <kernel/task/task.hpp>
#include <kernel/task/scheduler.hpp>
#include <lib/unique_ptr.hpp>

using namespace kernel;

/// @brief  Deleter for a TaskControlBlock that was added to the scheduler.
///         Calls remove_task() + cleanup() + delete.
struct TaskDeleter {
    void operator()(TaskControlBlock *tcb) {
        if (tcb) {
            Scheduler::remove_task(*tcb);
            tcb->cleanup();
            delete tcb;
        }
    }
};

/// @brief  Deleter for a TaskControlBlock that was NOT added to the scheduler.
///         Calls cleanup() + delete only.
struct SimpleTaskDeleter {
    void operator()(TaskControlBlock *tcb) {
        if (tcb) {
            tcb->cleanup();
            delete tcb;
        }
    }
};

/// @brief  owning pointer for a scheduler-registered task.
using TaskPtr = UniquePtr<TaskControlBlock, TaskDeleter>;

/// @brief  owning pointer for a stand-alone task (not in scheduler).
using SimpleTaskPtr = UniquePtr<TaskControlBlock, SimpleTaskDeleter>;
