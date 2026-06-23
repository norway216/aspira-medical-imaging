/**
 * @file thread_pool.c
 * @brief Priority-based thread pool implementation
 *
 * Workers drain higher-priority queues first. Each worker:
 * 1. Check ACQUISITION queue → PROCESSING queue → RENDERING queue
 * 2. If all empty, spin briefly then yield
 * 3. On wakeup: check queues again
 *
 * Worker threads use a condition variable for efficient waiting
 * when all queues are empty, with a spin phase for low-latency wakeup.
 */

#include "aspira/core/thread_pool.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Worker thread argument */
typedef struct {
    aspira_thread_pool* tp;
    int                 thread_id;
    size_t              thread_index;
} worker_args_t;

/* Condition variable for idle workers */
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    bool            signaled;
} worker_signal_t;

/* Worker callback function */
static void* worker_thread(void* arg) {
    worker_args_t* wargs = (worker_args_t*)arg;
    aspira_thread_pool* tp = wargs->tp;
    int tid = wargs->thread_id;

    /* Set CPU affinity if configured */
    if (tp->has_affinity && tp->cpu_affinities) {
        pthread_setaffinity_np(pthread_self(),
                                sizeof(cpu_set_t),
                                &tp->cpu_affinities[wargs->thread_index]);
    }

    aspira_task task;

    while (atomic_load_explicit(&tp->running, memory_order_acquire)) {
        bool got_task = false;

        /* Drain queues in priority order */
        for (int p = 0; p < ASPIRA_PRIORITY_COUNT; p++) {
            if (aspira_mpmc_dequeue(&tp->queues[p], &task)) {
                got_task = true;
                break;
            }
        }

        if (got_task) {
            /* Execute the task */
            if (task.function) {
                task.function(task.arg);
            }
            atomic_fetch_sub_explicit(&tp->tasks_pending, 1,
                                       memory_order_release);
        } else {
            /* No tasks available — spin then yield to avoid burning CPU */
            for (int spin = 0; spin < 100; spin++) {
                /* Check queues again during spin */
                for (int p = 0; p < ASPIRA_PRIORITY_COUNT; p++) {
                    if (aspira_mpmc_dequeue(&tp->queues[p], &task)) {
                        got_task = true;
                        break;
                    }
                }
                if (got_task) break;
            }

            if (!got_task) {
                /* Yield CPU to other threads */
                struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000 }; /* 100us */
                nanosleep(&ts, NULL);
            }
        }

        (void)tid; /* Suppress unused warning */
    }

    return NULL;
}

/* ==========================================================================
 * Thread Pool Implementation
 * ========================================================================== */

bool aspira_thread_pool_init(aspira_thread_pool* tp, size_t num_threads,
                              uint64_t queue_capacity) {
    if (!tp || num_threads == 0 || num_threads > ASPIRA_MAX_THREADS) {
        return false;
    }

    memset(tp, 0, sizeof(*tp));
    tp->num_threads = num_threads;

    /* Initialize per-priority MPMC queues */
    for (int p = 0; p < ASPIRA_PRIORITY_COUNT; p++) {
        if (!aspira_mpmc_init(&tp->queues[p], queue_capacity,
                               sizeof(aspira_task))) {
            /* Cleanup already-initialized queues */
            for (int j = 0; j < p; j++) {
                aspira_mpmc_destroy(&tp->queues[j]);
            }
            return false;
        }
    }

    /* Allocate thread handles */
    tp->threads = (pthread_t*)calloc(num_threads, sizeof(pthread_t));
    tp->thread_ids = (int*)calloc(num_threads, sizeof(int));
    tp->cpu_affinities = (cpu_set_t*)calloc(num_threads, sizeof(cpu_set_t));

    if (!tp->threads || !tp->thread_ids || !tp->cpu_affinities) {
        free(tp->threads);
        free(tp->thread_ids);
        free(tp->cpu_affinities);
        for (int p = 0; p < ASPIRA_PRIORITY_COUNT; p++) {
            aspira_mpmc_destroy(&tp->queues[p]);
        }
        return false;
    }

    tp->has_affinity = false;
    atomic_init(&tp->running, true);
    atomic_init(&tp->tasks_pending, 0);

    /* Spawn worker threads */
    for (size_t i = 0; i < num_threads; i++) {
        worker_args_t* wargs = (worker_args_t*)malloc(sizeof(worker_args_t));
        if (!wargs) {
            /* Signal shutdown for already-started threads */
            atomic_store_explicit(&tp->running, false, memory_order_release);
            for (size_t j = 0; j < i; j++) {
                pthread_join(tp->threads[j], NULL);
            }
            free(tp->threads);
            free(tp->thread_ids);
            free(tp->cpu_affinities);
            for (int p = 0; p < ASPIRA_PRIORITY_COUNT; p++) {
                aspira_mpmc_destroy(&tp->queues[p]);
            }
            return false;
        }

        wargs->tp = tp;
        wargs->thread_id = (int)i;
        wargs->thread_index = i;

        if (pthread_create(&tp->threads[i], NULL, worker_thread, wargs) != 0) {
            free(wargs);
            atomic_store_explicit(&tp->running, false, memory_order_release);
            for (size_t j = 0; j < i; j++) {
                pthread_join(tp->threads[j], NULL);
            }
            free(tp->threads);
            free(tp->thread_ids);
            free(tp->cpu_affinities);
            for (int p = 0; p < ASPIRA_PRIORITY_COUNT; p++) {
                aspira_mpmc_destroy(&tp->queues[p]);
            }
            return false;
        }
    }

    return true;
}

void aspira_thread_pool_destroy(aspira_thread_pool* tp) {
    if (!tp) return;

    /* Signal shutdown */
    atomic_store_explicit(&tp->running, false, memory_order_release);

    /* Wait for all worker threads to finish */
    for (size_t i = 0; i < tp->num_threads; i++) {
        pthread_join(tp->threads[i], NULL);
    }

    /* Free resources */
    for (int p = 0; p < ASPIRA_PRIORITY_COUNT; p++) {
        aspira_mpmc_destroy(&tp->queues[p]);
    }

    free(tp->threads);
    free(tp->thread_ids);
    free(tp->cpu_affinities);
    tp->threads = NULL;
    tp->thread_ids = NULL;
    tp->cpu_affinities = NULL;
}

bool aspira_thread_pool_set_affinity(aspira_thread_pool* tp,
                                      size_t thread_index, int cpu_core) {
    if (!tp || thread_index >= tp->num_threads || cpu_core < 0) {
        return false;
    }

    CPU_ZERO(&tp->cpu_affinities[thread_index]);
    CPU_SET((int)cpu_core, &tp->cpu_affinities[thread_index]);
    tp->has_affinity = true;

    /* Apply immediately to the running thread */
    int ret = pthread_setaffinity_np(tp->threads[thread_index],
                                      sizeof(cpu_set_t),
                                      &tp->cpu_affinities[thread_index]);
    return ret == 0;
}

bool aspira_thread_pool_enqueue(aspira_thread_pool* tp,
                                 aspira_priority_t priority,
                                 aspira_task_fn func, void* arg) {
    if (!tp || !func) return false;

    aspira_task task;
    task.function = func;
    task.arg = arg;

    if (!aspira_mpmc_enqueue(&tp->queues[priority], &task)) {
        tp->stats.queue_full_events++;
        tp->stats.tasks_dropped++;
        return false;
    }

    atomic_fetch_add_explicit(&tp->tasks_pending, 1, memory_order_release);
    tp->stats.tasks_enqueued++;
    return true;
}

bool aspira_thread_pool_enqueue_timed(aspira_thread_pool* tp,
                                       aspira_priority_t priority,
                                       aspira_task_fn func, void* arg,
                                       uint64_t timeout_ns) {
    if (!tp || !func) return false;

    aspira_task task;
    task.function = func;
    task.arg = arg;

    if (!aspira_mpmc_enqueue_timed(&tp->queues[priority], &task, timeout_ns)) {
        tp->stats.queue_full_events++;
        tp->stats.tasks_dropped++;
        return false;
    }

    atomic_fetch_add_explicit(&tp->tasks_pending, 1, memory_order_release);
    tp->stats.tasks_enqueued++;
    return true;
}

void aspira_thread_pool_wait(aspira_thread_pool* tp) {
    if (!tp) return;

    /* Busy-wait for all pending tasks to complete */
    while (atomic_load_explicit(&tp->tasks_pending, memory_order_acquire) > 0) {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000 }; /* 100us */
        nanosleep(&ts, NULL);
    }
}

uint64_t aspira_thread_pool_pending_count(const aspira_thread_pool* tp) {
    if (!tp) return 0;
    return atomic_load_explicit(&tp->tasks_pending, memory_order_acquire);
}

void aspira_thread_pool_get_stats(const aspira_thread_pool* tp,
                                   aspira_thread_pool_stats* stats) {
    if (!tp || !stats) return;
    memcpy(stats, &tp->stats, sizeof(*stats));
}
