/**
 * @file thread_pool.h
 * @brief Priority-based thread pool with CPU affinity support
 *
 * The thread pool maintains N worker threads that consume tasks from
 * MPMC queues organized by priority:
 *   ACQUISITION (highest) > PROCESSING (normal) > RENDERING (lowest)
 *
 * Workers always drain higher-priority queues first. CPU affinity
 * can be set per-thread to pin workers to specific cores.
 */

#ifndef ASPIRA_THREAD_POOL_H
#define ASPIRA_THREAD_POOL_H

#include <pthread.h>
#include <sched.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ring_buffer.h"

/* Max threads (must match core.h) */
#ifndef ASPIRA_MAX_THREADS
#define ASPIRA_MAX_THREADS 128
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Task function signature */
typedef void (*aspira_task_fn)(void* arg);

/**
 * @brief Task priority levels
 */
typedef enum {
    ASPIRA_PRIORITY_ACQUISITION = 0,  /* Highest - real-time data capture */
    ASPIRA_PRIORITY_PROCESSING  = 1,  /* Normal - signal processing */
    ASPIRA_PRIORITY_RENDERING   = 2,  /* Lowest - display updates */
    ASPIRA_PRIORITY_COUNT       = 3
} aspira_priority_t;

/**
 * @brief A single task in the queue
 */
typedef struct {
    aspira_task_fn function;
    void*          arg;
} aspira_task;

/**
 * @brief Statistics for the thread pool
 */
typedef struct {
    uint64_t tasks_enqueued;
    uint64_t tasks_completed;
    uint64_t tasks_dropped;
    uint64_t queue_full_events;
    uint64_t total_idle_spins;
} aspira_thread_pool_stats;

/**
 * @brief Priority thread pool
 */
typedef struct aspira_thread_pool {
    pthread_t*       threads;          /* Worker threads */
    size_t           num_threads;

    aspira_mpmc_rb   queues[ASPIRA_PRIORITY_COUNT]; /* One MPMC queue per priority */

    alignas(64) atomic_bool running;
    alignas(64) atomic_int tasks_pending;

    /* CPU affinity: one cpu_set_t per thread (NULL if not set) */
    cpu_set_t*        cpu_affinities;
    bool              has_affinity;

    /* Thread worker indices (for affinity setup) */
    int*              thread_ids;

    /* Statistics */
    aspira_thread_pool_stats stats;
} aspira_thread_pool;

/**
 * @brief Initialize thread pool
 * @param tp Pointer to uninitialized thread pool
 * @param num_threads Number of worker threads (1..ASPIRA_MAX_THREADS)
 * @param queue_capacity Max tasks per priority queue (power of 2)
 * @return true on success
 */
bool aspira_thread_pool_init(aspira_thread_pool* tp, size_t num_threads,
                              uint64_t queue_capacity);

/**
 * @brief Destroy thread pool, wait for workers to finish
 */
void aspira_thread_pool_destroy(aspira_thread_pool* tp);

/**
 * @brief Set CPU affinity for a specific worker thread
 * @param tp Thread pool
 * @param thread_index Worker index (0..num_threads-1)
 * @param cpu_core CPU core to pin this thread to
 * @return true on success
 */
bool aspira_thread_pool_set_affinity(aspira_thread_pool* tp,
                                      size_t thread_index, int cpu_core);

/**
 * @brief Enqueue a task with given priority
 * @return true if enqueued, false if queue is full
 */
bool aspira_thread_pool_enqueue(aspira_thread_pool* tp,
                                 aspira_priority_t priority,
                                 aspira_task_fn func, void* arg);

/**
 * @brief Enqueue with timeout (spins for timeout_ns ns if queue full)
 * @return true if enqueued, false if timed out
 */
bool aspira_thread_pool_enqueue_timed(aspira_thread_pool* tp,
                                       aspira_priority_t priority,
                                       aspira_task_fn func, void* arg,
                                       uint64_t timeout_ns);

/**
 * @brief Wait until all pending tasks are completed
 */
void aspira_thread_pool_wait(aspira_thread_pool* tp);

/**
 * @brief Get number of pending tasks across all queues
 */
uint64_t aspira_thread_pool_pending_count(const aspira_thread_pool* tp);

/**
 * @brief Get statistics for the thread pool
 */
void aspira_thread_pool_get_stats(const aspira_thread_pool* tp,
                                   aspira_thread_pool_stats* stats);

#ifdef __cplusplus
}
#endif

#endif /* ASPIRA_THREAD_POOL_H */
