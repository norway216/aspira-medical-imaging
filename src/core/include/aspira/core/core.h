/**
 * @file core.h
 * @brief Aspira Core — Master header.
 *        C mode: includes sub-headers (full structs + API).
 *        C++ mode: opaque types + extern "C" API only.
 */

#ifndef ASPIRA_CORE_H
#define ASPIRA_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ==========================================================================
 * Shared Constants
 * ========================================================================== */
#define ASPIRA_CACHE_LINE_SIZE 64
#define ASPIRA_MAX_THREADS      128
#define ASPIRA_MAX_PRIORITIES   3
#define ASPIRA_MAX_FILTERS      16
#define ASPIRA_MAX_WATCHDOGS    8

/* ==========================================================================
 * frame.h is always included (no atomics, safe for C++ too)
 * ========================================================================== */
#include "frame.h"

/* ==========================================================================
 * C mode: include full struct definitions from C11-atomics sub-headers
 * ========================================================================== */
#ifndef __cplusplus
#include "ring_buffer.h"
#include "memory_pool.h"
#include "thread_pool.h"
#include "double_buffer.h"
#include "signal_pipeline.h"
#include "watchdog.h"
#include "ipc.h"
#endif

/* ==========================================================================
 * C++ mode: opaque types + enum definitions
 * ========================================================================== */
#ifdef __cplusplus
extern "C" {
#endif

/* Error codes (used by both C and C++) */
typedef enum {
    ASPIRA_OK = 0,
    ASPIRA_ERROR_NULL_POINTER = -1,
    ASPIRA_ERROR_INVALID_ARG  = -2,
    ASPIRA_ERROR_NO_MEMORY    = -3,
    ASPIRA_ERROR_FULL         = -4,
    ASPIRA_ERROR_EMPTY        = -5,
    ASPIRA_ERROR_TIMEOUT      = -6,
    ASPIRA_ERROR_BUSY         = -7,
    ASPIRA_ERROR_NOT_FOUND    = -8,
    ASPIRA_ERROR_OVERFLOW     = -9,
    ASPIRA_ERROR_INTERNAL     = -10,
} aspira_error_t;

const char* aspira_strerror(aspira_error_t err);
const char* aspira_version(void);

#ifdef __cplusplus
/* C++ only: opaque forward declarations (frame.h is included for both) */
typedef struct aspira_spsc_rb             aspira_spsc_rb;
typedef struct aspira_mpmc_rb             aspira_mpmc_rb;
typedef struct aspira_mpmc_slot           aspira_mpmc_slot;
typedef struct aspira_memory_pool         aspira_memory_pool;
typedef struct aspira_frame_pool          aspira_frame_pool;
typedef struct aspira_thread_pool         aspira_thread_pool;
typedef struct aspira_double_buffer       aspira_double_buffer;
typedef struct aspira_filter_node         aspira_filter_node;
typedef struct aspira_pipeline            aspira_pipeline;
typedef struct aspira_watchdog            aspira_watchdog;
typedef struct aspira_watchdog_manager    aspira_watchdog_manager;
typedef struct aspira_ipc_shm_header      aspira_ipc_shm_header;
typedef struct aspira_ipc_channel         aspira_ipc_channel;

/* C++ only: enum definitions (C gets them from sub-headers)
   Frame enums are defined in frame.h which is always included.
   Other enums needed by the API: */
typedef enum {
    ASPIRA_PRIORITY_ACQUISITION = 0,
    ASPIRA_PRIORITY_PROCESSING  = 1,
    ASPIRA_PRIORITY_RENDERING   = 2,
    ASPIRA_PRIORITY_COUNT       = 3
} aspira_priority_t;

typedef enum {
    ASPIRA_WATCHDOG_OK       = 0,
    ASPIRA_WATCHDOG_WARNING  = 1,
    ASPIRA_WATCHDOG_TIMEOUT  = 2,
    ASPIRA_WATCHDOG_CRITICAL = 3,
} aspira_watchdog_state_t;

typedef enum {
    ASPIRA_FILTER_FIR = 0,
    ASPIRA_FILTER_IIR,
    ASPIRA_FILTER_ENVELOPE,
    ASPIRA_FILTER_DOWNSAMPLE,
    ASPIRA_FILTER_BEAMFORM,
    ASPIRA_FILTER_SCAN_CONVERT,
    ASPIRA_FILTER_GAIN,
    ASPIRA_FILTER_DC_REMOVE,
} aspira_filter_type_t;

typedef enum {
    ASPIRA_IPC_MSG_FRAME_READY = 1,
    ASPIRA_IPC_MSG_START       = 2,
    ASPIRA_IPC_MSG_STOP        = 3,
    ASPIRA_IPC_MSG_SHUTDOWN    = 4,
    ASPIRA_IPC_MSG_ACK         = 5,
    ASPIRA_IPC_MSG_ERROR       = 6,
} aspira_ipc_msg_type_t;

/* C++ only: callback typedefs */
typedef void (*aspira_task_fn)(void* arg);

typedef void (*aspira_watchdog_callback_fn)(const char* source_name,
                                             aspira_watchdog_state_t state,
                                             uint64_t last_heartbeat_ns,
                                             void* user_data);
#endif /* __cplusplus */

/* ==========================================================================
 * Public API — All C function declarations (extern "C" for C++ compat)
 * ========================================================================== */

/* --- Frame --- */
void aspira_frame_init(aspira_frame* frame, uint32_t width, uint32_t height,
                       uint32_t channels, float* data, size_t data_size);
void aspira_frame_reset(aspira_frame* frame);

/* --- SPSC Ring Buffer --- */
aspira_spsc_rb* aspira_spsc_create(uint64_t capacity, size_t element_size);
void aspira_spsc_free(aspira_spsc_rb* rb);
bool aspira_spsc_init(aspira_spsc_rb* rb, uint64_t capacity, size_t element_size);
void aspira_spsc_destroy(aspira_spsc_rb* rb);
bool aspira_spsc_push(aspira_spsc_rb* rb, const void* element);
bool aspira_spsc_pop(aspira_spsc_rb* rb, void* element);
const void* aspira_spsc_front(const aspira_spsc_rb* rb);
void aspira_spsc_pop_front(aspira_spsc_rb* rb);
uint64_t aspira_spsc_count(const aspira_spsc_rb* rb);
bool aspira_spsc_empty(const aspira_spsc_rb* rb);
bool aspira_spsc_full(const aspira_spsc_rb* rb);
uint64_t aspira_spsc_capacity(const aspira_spsc_rb* rb);

/* --- MPMC Ring Buffer --- */
aspira_mpmc_rb* aspira_mpmc_create(uint64_t capacity, size_t element_size);
void aspira_mpmc_free(aspira_mpmc_rb* rb);
bool aspira_mpmc_init(aspira_mpmc_rb* rb, uint64_t capacity, size_t element_size);
void aspira_mpmc_destroy(aspira_mpmc_rb* rb);
bool aspira_mpmc_enqueue(aspira_mpmc_rb* rb, const void* element);
bool aspira_mpmc_dequeue(aspira_mpmc_rb* rb, void* element);
bool aspira_mpmc_enqueue_timed(aspira_mpmc_rb* rb, const void* element, uint64_t timeout_ns);
bool aspira_mpmc_dequeue_timed(aspira_mpmc_rb* rb, void* element, uint64_t timeout_ns);
uint64_t aspira_mpmc_count(const aspira_mpmc_rb* rb);
bool aspira_mpmc_empty(const aspira_mpmc_rb* rb);
bool aspira_mpmc_full(const aspira_mpmc_rb* rb);

/* --- Memory Pool --- */
aspira_memory_pool* aspira_memory_pool_create(size_t block_size, size_t num_blocks);
void aspira_memory_pool_free_ptr(aspira_memory_pool* pool);
bool aspira_memory_pool_init(aspira_memory_pool* pool, size_t block_size, size_t num_blocks);
bool aspira_memory_pool_init_numa(aspira_memory_pool* pool, size_t block_size,
                                   size_t num_blocks, int numa_node);
void aspira_memory_pool_destroy(aspira_memory_pool* pool);
void* aspira_memory_pool_alloc(aspira_memory_pool* pool);
void aspira_memory_pool_free(aspira_memory_pool* pool, void* ptr);
size_t aspira_memory_pool_free_count(const aspira_memory_pool* pool);
size_t aspira_memory_pool_allocated_count(const aspira_memory_pool* pool);
size_t aspira_memory_pool_block_size(const aspira_memory_pool* pool);

/* --- Frame Pool --- */
aspira_frame_pool* aspira_frame_pool_create(size_t num_frames, uint32_t frame_width,
                                             uint32_t frame_height, uint32_t frame_channels);
void aspira_frame_pool_free_ptr(aspira_frame_pool* pool);
bool aspira_frame_pool_init(aspira_frame_pool* pool, size_t num_frames,
                             uint32_t frame_width, uint32_t frame_height,
                             uint32_t frame_channels);
void aspira_frame_pool_destroy(aspira_frame_pool* pool);
aspira_frame* aspira_frame_pool_alloc_frame(aspira_frame_pool* pool);
void aspira_frame_pool_free_frame(aspira_frame_pool* pool, aspira_frame* frame);
size_t aspira_frame_pool_free_count(const aspira_frame_pool* pool);

/* --- Thread Pool --- */
aspira_thread_pool* aspira_thread_pool_create(size_t num_threads, uint64_t queue_capacity);
void aspira_thread_pool_free_ptr(aspira_thread_pool* tp);
bool aspira_thread_pool_init(aspira_thread_pool* tp, size_t num_threads,
                              uint64_t queue_capacity);
void aspira_thread_pool_destroy(aspira_thread_pool* tp);
bool aspira_thread_pool_set_affinity(aspira_thread_pool* tp, size_t thread_index, int cpu_core);
bool aspira_thread_pool_enqueue(aspira_thread_pool* tp, aspira_priority_t priority,
                                 aspira_task_fn func, void* arg);
bool aspira_thread_pool_enqueue_timed(aspira_thread_pool* tp, aspira_priority_t priority,
                                       aspira_task_fn func, void* arg, uint64_t timeout_ns);
void aspira_thread_pool_wait(aspira_thread_pool* tp);
uint64_t aspira_thread_pool_pending_count(const aspira_thread_pool* tp);

/* --- Double Buffer --- */
aspira_double_buffer* aspira_double_buffer_create(void);
void aspira_double_buffer_free_ptr(aspira_double_buffer* db);
void aspira_double_buffer_init(aspira_double_buffer* db, aspira_frame* front, aspira_frame* back);
void aspira_double_buffer_destroy(aspira_double_buffer* db);
const aspira_frame* aspira_double_buffer_read(aspira_double_buffer* db);
aspira_frame* aspira_double_buffer_write_begin(aspira_double_buffer* db);
void aspira_double_buffer_swap(aspira_double_buffer* db);
bool aspira_double_buffer_has_new_data(const aspira_double_buffer* db);
void aspira_double_buffer_mark_consumed(aspira_double_buffer* db);

/* --- Signal Pipeline --- */
aspira_pipeline* aspira_pipeline_create(void);
void aspira_pipeline_free_ptr(aspira_pipeline* pipeline);
void aspira_pipeline_init(aspira_pipeline* pipeline);
void aspira_pipeline_destroy(aspira_pipeline* pipeline);
bool aspira_pipeline_add_fir(aspira_pipeline* pipeline, const float* coeffs,
                              size_t num_taps, const char* name);
bool aspira_pipeline_add_iir(aspira_pipeline* pipeline, const float* b_coeffs,
                              const float* a_coeffs, size_t order, const char* name);
bool aspira_pipeline_add_envelope(aspira_pipeline* pipeline, const char* name);
bool aspira_pipeline_add_downsample(aspira_pipeline* pipeline, uint32_t factor,
                                     const char* name);
bool aspira_pipeline_add_beamform(aspira_pipeline* pipeline, const float* delays,
                                   uint32_t num_elements, uint32_t num_lines,
                                   const char* name);
bool aspira_pipeline_add_gain(aspira_pipeline* pipeline, float gain_db, const char* name);
bool aspira_pipeline_add_dc_remove(aspira_pipeline* pipeline, const char* name);
bool aspira_pipeline_process(aspira_pipeline* pipeline, const aspira_frame* input,
                              aspira_frame* output);
bool aspira_pipeline_process_inplace(aspira_pipeline* pipeline, aspira_frame* frame);
size_t aspira_pipeline_filter_count(const aspira_pipeline* pipeline);
void aspira_pipeline_reset(aspira_pipeline* pipeline);

/* Standalone filters */
void aspira_fir_apply(const float* input, float* output, size_t length,
                      const float* coeffs, size_t num_taps);
void aspira_iir_apply(const float* input, float* output, size_t length,
                      const float* b_coeffs, const float* a_coeffs, size_t order);
void aspira_envelope_detect(const float* input, float* output, size_t length);
void aspira_dc_remove(float* data, size_t length);

/* --- Watchdog --- */
aspira_watchdog* aspira_watchdog_create(const char* name, uint64_t timeout_ms,
                                         aspira_watchdog_callback_fn callback, void* user_data);
void aspira_watchdog_free_ptr(aspira_watchdog* wd);
bool aspira_watchdog_init(aspira_watchdog* wd, const char* name, uint64_t timeout_ms,
                           aspira_watchdog_callback_fn callback, void* user_data);
void aspira_watchdog_destroy(aspira_watchdog* wd);
void aspira_watchdog_pet(aspira_watchdog* wd);
aspira_watchdog_state_t aspira_watchdog_get_state(const aspira_watchdog* wd);
uint64_t aspira_watchdog_time_since_pet(const aspira_watchdog* wd);
bool aspira_watchdog_is_healthy(const aspira_watchdog* wd);

/* Watchdog Manager */
aspira_watchdog_manager* aspira_watchdog_manager_create(uint64_t check_interval_ms);
void aspira_watchdog_manager_free_ptr(aspira_watchdog_manager* mgr);
bool aspira_watchdog_manager_init(aspira_watchdog_manager* mgr, uint64_t check_interval_ms);
void aspira_watchdog_manager_destroy(aspira_watchdog_manager* mgr);
bool aspira_watchdog_manager_register(aspira_watchdog_manager* mgr, aspira_watchdog* wd);
bool aspira_watchdog_manager_healthy(const aspira_watchdog_manager* mgr);
size_t aspira_watchdog_manager_count(const aspira_watchdog_manager* mgr);

/* --- IPC --- */
bool aspira_ipc_producer_create(aspira_ipc_channel* channel, const char* name,
                                 uint32_t max_frames, uint32_t frame_width,
                                 uint32_t frame_height, uint32_t frame_channels);
bool aspira_ipc_consumer_open(aspira_ipc_channel* channel, const char* name);
void aspira_ipc_producer_destroy(aspira_ipc_channel* channel);
void aspira_ipc_consumer_close(aspira_ipc_channel* channel);

#ifdef __cplusplus
}
#endif

#endif /* ASPIRA_CORE_H */
