/**
 * @file module_wrapper.cpp
 * @brief Implementation of C++ module wrappers
 */

#include "aspira/services/module_wrapper.h"

#include <cstdlib>
#include <new>

namespace aspira {

/* ==========================================================================
 * ThreadPool std::function support
 * ========================================================================== */

/* Task wrapper that holds a std::function and frees itself after execution */
struct TaskWrapper {
    std::function<void()> func;
};

static void task_thunk(void* arg) {
    TaskWrapper* wrapper = static_cast<TaskWrapper*>(arg);
    if (wrapper->func) {
        wrapper->func();
    }
    delete wrapper;
}

bool ThreadPool::enqueue(std::function<void()> task, aspira_priority_t prio) {
    TaskWrapper* wrapper = new (std::nothrow) TaskWrapper{std::move(task)};
    if (!wrapper) return false;

    if (!aspira_thread_pool_enqueue(tp_, prio, task_thunk, wrapper)) {
        delete wrapper;
        return false;
    }
    return true;
}

} // namespace aspira
