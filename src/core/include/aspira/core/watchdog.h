/**
 * @file watchdog.h
 * @brief Watchdog heartbeat monitor for pipeline fault detection
 *
 * Each pipeline stage has a watchdog that must be "pet" (heartbeat)
 * at regular intervals. If the timeout expires, the watchdog triggers
 * a callback to signal a fault, enabling graceful degradation.
 */

#ifndef ASPIRA_WATCHDOG_H
#define ASPIRA_WATCHDOG_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Watchdog states */
typedef enum {
    ASPIRA_WATCHDOG_OK       = 0,
    ASPIRA_WATCHDOG_WARNING  = 1,  /* Approaching timeout */
    ASPIRA_WATCHDOG_TIMEOUT  = 2,  /* Timeout reached */
    ASPIRA_WATCHDOG_CRITICAL = 3,  /* Multiple consecutive timeouts */
} aspira_watchdog_state_t;

/* Callback when watchdog state changes */
typedef void (*aspira_watchdog_callback_fn)(const char* source_name,
                                             aspira_watchdog_state_t state,
                                             uint64_t last_heartbeat_ns,
                                             void* user_data);

/**
 * @brief Single watchdog instance for one pipeline stage
 */
typedef struct aspira_watchdog {
    char            name[32];               /* Stage name for diagnostics */
    atomic_uint_least64_t last_heartbeat_ns; /* Last pet timestamp */
    atomic_uint_least64_t timeout_ns;        /* Timeout period */
    atomic_uint_least64_t warning_threshold_ns; /* Warning at 80% of timeout */

    pthread_t       monitor_thread;
    atomic_bool     active;
    atomic_int      missed_count;           /* Consecutive missed heartbeats */

    aspira_watchdog_callback_fn callback;
    void*           user_data;

    /* State tracking */
    atomic_int      current_state;          /* aspira_watchdog_state_t */
} aspira_watchdog;

/**
 * @brief Initialize a watchdog
 * @param wd Pointer to uninitialized watchdog
 * @param name Human-readable name for this watchdog
 * @param timeout_ms Timeout in milliseconds
 * @param callback Function to call on state change (can be NULL)
 * @param user_data User data passed to callback
 * @return true on success
 */
bool aspira_watchdog_init(aspira_watchdog* wd, const char* name,
                           uint64_t timeout_ms,
                           aspira_watchdog_callback_fn callback,
                           void* user_data);

/**
 * @brief Destroy watchdog and stop monitor thread
 */
void aspira_watchdog_destroy(aspira_watchdog* wd);

/**
 * @brief Pet the watchdog (signal healthy heartbeat)
 *
 * Call this from the monitored thread at regular intervals.
 * The interval should be significantly less than the timeout.
 */
void aspira_watchdog_pet(aspira_watchdog* wd);

/**
 * @brief Get current watchdog state
 */
aspira_watchdog_state_t aspira_watchdog_get_state(const aspira_watchdog* wd);

/**
 * @brief Get time since last heartbeat in nanoseconds
 */
uint64_t aspira_watchdog_time_since_pet(const aspira_watchdog* wd);

/**
 * @brief Check if watchdog is healthy
 */
bool aspira_watchdog_is_healthy(const aspira_watchdog* wd);

/* ==========================================================================
 * Watchdog Manager (pipeline-wide supervision)
 * ========================================================================== */

#define ASPIRA_WATCHDOG_MANAGER_MAX 8

/**
 * @brief Manages multiple watchdogs for a full pipeline
 */
typedef struct aspira_watchdog_manager {
    aspira_watchdog* watchdogs[ASPIRA_WATCHDOG_MANAGER_MAX];
    size_t           num_watchdogs;
    pthread_t        supervisor_thread;
    atomic_bool      active;
    atomic_bool      pipeline_healthy;
    uint64_t         supervisor_interval_ns;  /* How often to check */
} aspira_watchdog_manager;

/**
 * @brief Initialize watchdog manager
 * @param mgr Pointer to uninitialized manager
 * @param check_interval_ms How often the supervisor checks all watchdogs
 */
bool aspira_watchdog_manager_init(aspira_watchdog_manager* mgr,
                                   uint64_t check_interval_ms);

/**
 * @brief Destroy watchdog manager and all managed watchdogs
 */
void aspira_watchdog_manager_destroy(aspira_watchdog_manager* mgr);

/**
 * @brief Register a watchdog with the manager (takes ownership)
 * @return true on success
 */
bool aspira_watchdog_manager_register(aspira_watchdog_manager* mgr,
                                       aspira_watchdog* wd);

/**
 * @brief Check if the entire pipeline is healthy
 */
bool aspira_watchdog_manager_healthy(const aspira_watchdog_manager* mgr);

/**
 * @brief Get number of registered watchdogs
 */
size_t aspira_watchdog_manager_count(const aspira_watchdog_manager* mgr);

#ifdef __cplusplus
}
#endif

#endif /* ASPIRA_WATCHDOG_H */
