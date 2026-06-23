/**
 * @file sizes.c
 * @brief Provide sizeof() for opaque types via C functions.
 *        This is the bridge between C (which knows struct sizes) and
 *        C++ (which only has opaque forward declarations).
 */
#include "aspira/core/core.h"
#include <stdlib.h>

/* SPSC Ring Buffer */
aspira_spsc_rb* aspira_spsc_create(uint64_t cap, size_t esz) {
    aspira_spsc_rb* rb = (aspira_spsc_rb*)malloc(sizeof(aspira_spsc_rb));
    if (!rb) return NULL;
    if (!aspira_spsc_init(rb, cap, esz)) { free(rb); return NULL; }
    return rb;
}
void aspira_spsc_free(aspira_spsc_rb* rb) { if (rb) { aspira_spsc_destroy(rb); free(rb); } }

/* MPMC Ring Buffer */
aspira_mpmc_rb* aspira_mpmc_create(uint64_t cap, size_t esz) {
    aspira_mpmc_rb* rb = (aspira_mpmc_rb*)malloc(sizeof(aspira_mpmc_rb));
    if (!rb) return NULL;
    if (!aspira_mpmc_init(rb, cap, esz)) { free(rb); return NULL; }
    return rb;
}
void aspira_mpmc_free(aspira_mpmc_rb* rb) { if (rb) { aspira_mpmc_destroy(rb); free(rb); } }

/* Memory Pool */
aspira_memory_pool* aspira_memory_pool_create(size_t bsz, size_t n) {
    aspira_memory_pool* p = (aspira_memory_pool*)malloc(sizeof(aspira_memory_pool));
    if (!p) return NULL;
    if (!aspira_memory_pool_init(p, bsz, n)) { free(p); return NULL; }
    return p;
}
void aspira_memory_pool_free_ptr(aspira_memory_pool* p) { if (p) { aspira_memory_pool_destroy(p); free(p); } }

/* Frame Pool */
aspira_frame_pool* aspira_frame_pool_create(size_t n, uint32_t w, uint32_t h, uint32_t c) {
    aspira_frame_pool* p = (aspira_frame_pool*)malloc(sizeof(aspira_frame_pool));
    if (!p) return NULL;
    if (!aspira_frame_pool_init(p, n, w, h, c)) { free(p); return NULL; }
    return p;
}
void aspira_frame_pool_free_ptr(aspira_frame_pool* p) { if (p) { aspira_frame_pool_destroy(p); free(p); } }

/* Thread Pool */
aspira_thread_pool* aspira_thread_pool_create(size_t n, uint64_t qc) {
    aspira_thread_pool* tp = (aspira_thread_pool*)malloc(sizeof(aspira_thread_pool));
    if (!tp) return NULL;
    if (!aspira_thread_pool_init(tp, n, qc)) { free(tp); return NULL; }
    return tp;
}
void aspira_thread_pool_free_ptr(aspira_thread_pool* tp) { if (tp) { aspira_thread_pool_destroy(tp); free(tp); } }

/* Double Buffer */
aspira_double_buffer* aspira_double_buffer_create(void) {
    aspira_double_buffer* db = (aspira_double_buffer*)calloc(1, sizeof(aspira_double_buffer));
    return db;
}
void aspira_double_buffer_free_ptr(aspira_double_buffer* db) { if (db) { aspira_double_buffer_destroy(db); free(db); } }

/* Signal Pipeline */
aspira_pipeline* aspira_pipeline_create(void) {
    aspira_pipeline* p = (aspira_pipeline*)calloc(1, sizeof(aspira_pipeline));
    if (p) aspira_pipeline_init(p);
    return p;
}
void aspira_pipeline_free_ptr(aspira_pipeline* p) { if (p) { aspira_pipeline_destroy(p); free(p); } }

/* Watchdog */
aspira_watchdog* aspira_watchdog_create(const char* name, uint64_t timeout_ms,
                                         aspira_watchdog_callback_fn cb, void* ud) {
    aspira_watchdog* wd = (aspira_watchdog*)malloc(sizeof(aspira_watchdog));
    if (!wd) return NULL;
    if (!aspira_watchdog_init(wd, name, timeout_ms, cb, ud)) { free(wd); return NULL; }
    return wd;
}
void aspira_watchdog_free_ptr(aspira_watchdog* wd) { if (wd) { aspira_watchdog_destroy(wd); free(wd); } }

/* Watchdog Manager */
aspira_watchdog_manager* aspira_watchdog_manager_create(uint64_t interval_ms) {
    aspira_watchdog_manager* mgr = (aspira_watchdog_manager*)malloc(sizeof(aspira_watchdog_manager));
    if (!mgr) return NULL;
    if (!aspira_watchdog_manager_init(mgr, interval_ms)) { free(mgr); return NULL; }
    return mgr;
}
void aspira_watchdog_manager_free_ptr(aspira_watchdog_manager* mgr) {
    if (mgr) { aspira_watchdog_manager_destroy(mgr); free(mgr); }
}
