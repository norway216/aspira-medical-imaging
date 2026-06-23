/**
 * @file ring_buffer.h
 * @brief Lock-free SPSC and MPMC ring buffers using C11 atomics
 *
 * SPSC (Single Producer, Single Consumer):
 *   - Wait-free, no CAS loops needed
 *   - Cache-line padded to prevent false sharing
 *   - Zero-copy pointer access via front()/pop_front()
 *
 * MPMC (Multi Producer, Multi Consumer):
 *   - Dmitry Vyukov's bounded MPMC queue
 *   - Lock-free with CAS on enqueue/dequeue positions
 *   - No ABA problem (sequence counters per slot)
 */

#ifndef ASPIRA_RING_BUFFER_H
#define ASPIRA_RING_BUFFER_H

#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * SPSC Ring Buffer (wait-free, single producer, single consumer)
 * ========================================================================== */

/**
 * @brief SPSC lock-free ring buffer
 *
 * Thread safety: ONE producer thread and ONE consumer thread.
 * Using multiple producers or consumers is undefined behavior.
 *
 * Capacity must be a power of 2.
 */
typedef struct aspira_spsc_rb {
    alignas(64) atomic_uint_least64_t head;   /* Producer index */
    alignas(64) atomic_uint_least64_t tail;   /* Consumer index */
    uint64_t     capacity;                     /* Must be power of 2 */
    uint64_t     mask;                         /* capacity - 1 */
    size_t       element_size;
    uint8_t*     buffer;                       /* element_size * capacity */
} aspira_spsc_rb;

/**
 * @brief Initialize SPSC ring buffer
 * @param rb Pointer to uninitialized buffer struct
 * @param capacity Number of elements (MUST be power of 2)
 * @param element_size Size of each element in bytes
 * @return true on success, false on invalid args or allocation failure
 */
bool aspira_spsc_init(aspira_spsc_rb* rb, uint64_t capacity, size_t element_size);

/**
 * @brief Destroy SPSC ring buffer and free resources
 */
void aspira_spsc_destroy(aspira_spsc_rb* rb);

/**
 * @brief Push an element (producer only)
 * @return true if pushed, false if buffer is full
 */
bool aspira_spsc_push(aspira_spsc_rb* rb, const void* element);

/**
 * @brief Pop an element (consumer only)
 * @return true if popped, false if buffer is empty
 */
bool aspira_spsc_pop(aspira_spsc_rb* rb, void* element);

/**
 * @brief Get read-only pointer to next element without removing (consumer only)
 * @return Pointer to element, or NULL if empty
 */
const void* aspira_spsc_front(const aspira_spsc_rb* rb);

/**
 * @brief Remove the front element after front() was called (consumer only)
 */
void aspira_spsc_pop_front(aspira_spsc_rb* rb);

/**
 * @brief Get number of elements currently in the buffer
 */
uint64_t aspira_spsc_count(const aspira_spsc_rb* rb);

/**
 * @brief Check if buffer is empty
 */
bool aspira_spsc_empty(const aspira_spsc_rb* rb);

/**
 * @brief Check if buffer is full
 */
bool aspira_spsc_full(const aspira_spsc_rb* rb);

/**
 * @brief Get buffer capacity
 */
uint64_t aspira_spsc_capacity(const aspira_spsc_rb* rb);

/* ==========================================================================
 * MPMC Ring Buffer (lock-free, multi producer, multi consumer)
 * ========================================================================== */

/**
 * @brief A single slot in the MPMC ring buffer
 */
typedef struct aspira_mpmc_slot {
    atomic_uint_least64_t seq;   /* Sequence counter */
    uint8_t              data[]; /* Variable-length element data */
} aspira_mpmc_slot;

/**
 * @brief MPMC lock-free ring buffer (Vyukov bounded queue)
 *
 * Thread safety: Multiple producers AND multiple consumers.
 *
 * Performance: O(1) enqueue/dequeue, lock-free with CAS.
 * Contention: CAS on head/tail may retry under high contention.
 */
typedef struct aspira_mpmc_rb {
    alignas(64) atomic_uint_least64_t enqueue_pos;
    alignas(64) atomic_uint_least64_t dequeue_pos;
    uint64_t     capacity;          /* Must be power of 2 */
    uint64_t     mask;              /* capacity - 1 */
    size_t       element_size;
    size_t       slot_size;         /* sizeof(mpmc_slot) + element_size */
    uint8_t*     buffer;            /* slot_size * capacity */
} aspira_mpmc_rb;

/**
 * @brief Initialize MPMC ring buffer
 * @param rb Pointer to uninitialized buffer struct
 * @param capacity Number of elements (MUST be power of 2)
 * @param element_size Size of each element in bytes
 * @return true on success
 */
bool aspira_mpmc_init(aspira_mpmc_rb* rb, uint64_t capacity, size_t element_size);

/**
 * @brief Destroy MPMC ring buffer and free resources
 */
void aspira_mpmc_destroy(aspira_mpmc_rb* rb);

/**
 * @brief Enqueue an element (any thread)
 * @return true if enqueued, false if buffer is full
 */
bool aspira_mpmc_enqueue(aspira_mpmc_rb* rb, const void* element);

/**
 * @brief Dequeue an element (any thread)
 * @return true if dequeued, false if buffer is empty
 */
bool aspira_mpmc_dequeue(aspira_mpmc_rb* rb, void* element);

/**
 * @brief Try to enqueue, spinning up to timeout_ns nanoseconds
 * @return true if enqueued, false if timed out
 */
bool aspira_mpmc_enqueue_timed(aspira_mpmc_rb* rb, const void* element,
                               uint64_t timeout_ns);

/**
 * @brief Try to dequeue, spinning up to timeout_ns nanoseconds
 * @return true if dequeued, false if timed out
 */
bool aspira_mpmc_dequeue_timed(aspira_mpmc_rb* rb, void* element,
                               uint64_t timeout_ns);

/**
 * @brief Get approximate count of elements in buffer
 * @note Approximate due to concurrent operations
 */
uint64_t aspira_mpmc_count(const aspira_mpmc_rb* rb);

/**
 * @brief Check if buffer appears empty (approximate)
 */
bool aspira_mpmc_empty(const aspira_mpmc_rb* rb);

/**
 * @brief Check if buffer appears full (approximate)
 */
bool aspira_mpmc_full(const aspira_mpmc_rb* rb);

#ifdef __cplusplus
}
#endif

#endif /* ASPIRA_RING_BUFFER_H */
