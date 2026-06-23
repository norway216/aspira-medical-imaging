/**
 * @file watchdog.c
 * @brief Watchdog heartbeat monitor implementation
 *
 * Each watchdog runs a monitor thread that periodically checks if the
 * heartbeat has been received within the timeout period. If not, it
 * transitions through WARNING -> TIMEOUT -> CRITICAL states and
 * invokes the registered callback.
 */

#include "aspira/core/watchdog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ==========================================================================
 * Single Watchdog
 * ========================================================================== */

static void* watchdog_monitor_thread(void* arg) {
    aspira_watchdog* wd = (aspira_watchdog*)arg;
    uint64_t timeout_ns = atomic_load_explicit(&wd->timeout_ns, memory_order_relaxed);
    uint64_t check_interval = timeout_ns / 10;  /* Check 10x per timeout */
    if (check_interval < 1000000UL) check_interval = 1000000UL; /* Min 1ms */

    while (atomic_load_explicit(&wd->active, memory_order_acquire)) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t now_ns = (uint64_t)now.tv_sec * 1000000000UL + (uint64_t)now.tv_nsec;

        uint64_t last_pet = atomic_load_explicit(&wd->last_heartbeat_ns,
                                                   memory_order_acquire);
        uint64_t elapsed = now_ns - last_pet;

        aspira_watchdog_state_t new_state;
        int missed = atomic_load_explicit(&wd->missed_count, memory_order_relaxed);

        if (elapsed > timeout_ns) {
            missed++;
            atomic_store_explicit(&wd->missed_count, missed, memory_order_relaxed);

            if (missed >= 5) {
                new_state = ASPIRA_WATCHDOG_CRITICAL;
            } else if (missed >= 2) {
                new_state = ASPIRA_WATCHDOG_TIMEOUT;
            } else {
                new_state = ASPIRA_WATCHDOG_WARNING;
            }
        } else if (elapsed > timeout_ns * 80 / 100) {
            new_state = ASPIRA_WATCHDOG_WARNING;
        } else {
            atomic_store_explicit(&wd->missed_count, 0, memory_order_relaxed);
            new_state = ASPIRA_WATCHDOG_OK;
        }

        /* Detect state transition */
        int old_state = atomic_exchange_explicit(&wd->current_state,
                                                  (int)new_state,
                                                  memory_order_acq_rel);
        if (old_state != (int)new_state && wd->callback) {
            wd->callback(wd->name, new_state, last_pet, wd->user_data);
        }

        /* Sleep for the check interval */
        struct timespec sleep_ts;
        sleep_ts.tv_sec = check_interval / 1000000000UL;
        sleep_ts.tv_nsec = check_interval % 1000000000UL;
        nanosleep(&sleep_ts, NULL);
    }

    return NULL;
}

bool aspira_watchdog_init(aspira_watchdog* wd, const char* name,
                           uint64_t timeout_ms,
                           aspira_watchdog_callback_fn callback,
                           void* user_data) {
    if (!wd || !name || timeout_ms == 0) return false;

    memset(wd, 0, sizeof(*wd));
    strncpy(wd->name, name, sizeof(wd->name) - 1);

    uint64_t timeout_ns = timeout_ms * 1000000UL;
    atomic_init(&wd->timeout_ns, timeout_ns);
    atomic_init(&wd->warning_threshold_ns, timeout_ns * 80 / 100);
    atomic_init(&wd->missed_count, 0);
    atomic_init(&wd->current_state, ASPIRA_WATCHDOG_OK);
    atomic_init(&wd->active, true);

    /* Set initial heartbeat to now */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t now_ns = (uint64_t)now.tv_sec * 1000000000UL + (uint64_t)now.tv_nsec;
    atomic_init(&wd->last_heartbeat_ns, now_ns);

    wd->callback = callback;
    wd->user_data = user_data;

    /* Start monitor thread */
    if (pthread_create(&wd->monitor_thread, NULL, watchdog_monitor_thread, wd) != 0) {
        return false;
    }

    return true;
}

void aspira_watchdog_destroy(aspira_watchdog* wd) {
    if (!wd) return;

    atomic_store_explicit(&wd->active, false, memory_order_release);
    pthread_join(wd->monitor_thread, NULL);
}

void aspira_watchdog_pet(aspira_watchdog* wd) {
    if (!wd) return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t now_ns = (uint64_t)now.tv_sec * 1000000000UL + (uint64_t)now.tv_nsec;

    atomic_store_explicit(&wd->last_heartbeat_ns, now_ns, memory_order_release);
}

aspira_watchdog_state_t aspira_watchdog_get_state(const aspira_watchdog* wd) {
    if (!wd) return ASPIRA_WATCHDOG_CRITICAL;
    return (aspira_watchdog_state_t)atomic_load_explicit(&wd->current_state,
                                                           memory_order_acquire);
}

uint64_t aspira_watchdog_time_since_pet(const aspira_watchdog* wd) {
    if (!wd) return UINT64_MAX;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t now_ns = (uint64_t)now.tv_sec * 1000000000UL + (uint64_t)now.tv_nsec;
    uint64_t last = atomic_load_explicit(&wd->last_heartbeat_ns, memory_order_acquire);

    return now_ns - last;
}

bool aspira_watchdog_is_healthy(const aspira_watchdog* wd) {
    aspira_watchdog_state_t state = aspira_watchdog_get_state(wd);
    return state == ASPIRA_WATCHDOG_OK || state == ASPIRA_WATCHDOG_WARNING;
}

/* ==========================================================================
 * Watchdog Manager
 * ========================================================================== */

static void default_wd_callback(const char* name, aspira_watchdog_state_t state,
                                  uint64_t last_hb, void* user_data) {
    (void)last_hb;
    (void)user_data;

    const char* state_str = "UNKNOWN";
    switch (state) {
    case ASPIRA_WATCHDOG_OK:       state_str = "OK"; break;
    case ASPIRA_WATCHDOG_WARNING:  state_str = "WARNING"; break;
    case ASPIRA_WATCHDOG_TIMEOUT:  state_str = "TIMEOUT"; break;
    case ASPIRA_WATCHDOG_CRITICAL: state_str = "CRITICAL"; break;
    }

    fprintf(stderr, "[Watchdog] %s: %s\n", name, state_str);
}

static void* supervisor_thread_fn(void* arg) {
    aspira_watchdog_manager* mgr = (aspira_watchdog_manager*)arg;
    uint64_t interval = mgr->supervisor_interval_ns;

    while (atomic_load_explicit(&mgr->active, memory_order_acquire)) {
        bool all_healthy = true;

        for (size_t i = 0; i < mgr->num_watchdogs; i++) {
            if (!aspira_watchdog_is_healthy(mgr->watchdogs[i])) {
                all_healthy = false;
                break;
            }
        }

        atomic_store_explicit(&mgr->pipeline_healthy, all_healthy,
                               memory_order_release);

        struct timespec sleep_ts;
        sleep_ts.tv_sec = interval / 1000000000UL;
        sleep_ts.tv_nsec = interval % 1000000000UL;
        nanosleep(&sleep_ts, NULL);
    }

    return NULL;
}

bool aspira_watchdog_manager_init(aspira_watchdog_manager* mgr,
                                   uint64_t check_interval_ms) {
    if (!mgr || check_interval_ms == 0) return false;

    memset(mgr, 0, sizeof(*mgr));
    mgr->num_watchdogs = 0;
    mgr->supervisor_interval_ns = check_interval_ms * 1000000UL;
    atomic_init(&mgr->active, true);
    atomic_init(&mgr->pipeline_healthy, true);

    if (pthread_create(&mgr->supervisor_thread, NULL,
                       supervisor_thread_fn, mgr) != 0) {
        return false;
    }

    return true;
}

void aspira_watchdog_manager_destroy(aspira_watchdog_manager* mgr) {
    if (!mgr) return;

    atomic_store_explicit(&mgr->active, false, memory_order_release);
    pthread_join(mgr->supervisor_thread, NULL);

    for (size_t i = 0; i < mgr->num_watchdogs; i++) {
        aspira_watchdog_destroy(mgr->watchdogs[i]);
        free(mgr->watchdogs[i]);
    }
    mgr->num_watchdogs = 0;
}

bool aspira_watchdog_manager_register(aspira_watchdog_manager* mgr,
                                       aspira_watchdog* wd) {
    if (!mgr || !wd) return false;
    if (mgr->num_watchdogs >= ASPIRA_WATCHDOG_MANAGER_MAX) return false;

    /* Set default callback if none registered */
    if (!wd->callback) {
        wd->callback = default_wd_callback;
    }

    mgr->watchdogs[mgr->num_watchdogs++] = wd;
    return true;
}

bool aspira_watchdog_manager_healthy(const aspira_watchdog_manager* mgr) {
    if (!mgr) return false;
    return atomic_load_explicit(&mgr->pipeline_healthy, memory_order_acquire);
}

size_t aspira_watchdog_manager_count(const aspira_watchdog_manager* mgr) {
    if (!mgr) return 0;
    return mgr->num_watchdogs;
}
