/**
 * @file ring_buffer.c
 * @brief Lock-free SPSC and MPMC ring buffer implementations
 *
 * SPSC: Wait-free single-producer single-consumer queue.
 *   - Head is only written by producer, read by consumer
 *   - Tail is only written by consumer, read by producer
 *   - No CAS needed — just atomic loads/stores with proper ordering
 *
 * MPMC: Dmitry Vyukov's bounded lock-free multi-producer multi-consumer queue.
 *   - Uses a slot array with sequence counters
 *   - CAS on enqueue_pos and dequeue_pos
 *   - No ABA problem due to monotonic sequence counters
 */

#include "aspira/core/ring_buffer.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Check if a number is a power of 2 */
static inline bool is_power_of_two(uint64_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

/* ==========================================================================
 * Utility: nanosecond sleep via clock_nanosleep
 * ========================================================================== */

static void nanosleep_spin(uint64_t ns) {
    if (ns == 0) return;
    struct timespec ts;
    ts.tv_sec = ns / 1000000000UL;
    ts.tv_nsec = ns % 1000000000UL;
    clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
}

/* ==========================================================================
 * SPSC Ring Buffer
 * ========================================================================== */

bool aspira_spsc_init(aspira_spsc_rb* rb, uint64_t capacity, size_t element_size) {
    if (!rb || capacity == 0 || element_size == 0) {
        return false;
    }
    if (!is_power_of_two(capacity)) {
        return false;  /* Capacity must be power of 2 */
    }

    atomic_init(&rb->head, 0);
    atomic_init(&rb->tail, 0);
    rb->capacity = capacity;
    rb->mask = capacity - 1;
    rb->element_size = element_size;

    rb->buffer = (uint8_t*)calloc(capacity, element_size);
    if (!rb->buffer) {
        return false;
    }
    return true;
}

void aspira_spsc_destroy(aspira_spsc_rb* rb) {
    if (!rb) return;
    free(rb->buffer);
    rb->buffer = NULL;
    rb->capacity = 0;
}

bool aspira_spsc_push(aspira_spsc_rb* rb, const void* element) {
    if (!rb || !element) return false;

    uint64_t head = atomic_load_explicit(&rb->head, memory_order_relaxed);
    uint64_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);

    if (head - tail >= rb->capacity) {
        return false;  /* Full */
    }

    uint64_t idx = head & rb->mask;
    memcpy(rb->buffer + idx * rb->element_size, element, rb->element_size);

    atomic_store_explicit(&rb->head, head + 1, memory_order_release);
    return true;
}

bool aspira_spsc_pop(aspira_spsc_rb* rb, void* element) {
    if (!rb || !element) return false;

    uint64_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    uint64_t head = atomic_load_explicit(&rb->head, memory_order_acquire);

    if (tail >= head) {
        return false;  /* Empty */
    }

    uint64_t idx = tail & rb->mask;
    memcpy(element, rb->buffer + idx * rb->element_size, rb->element_size);

    atomic_store_explicit(&rb->tail, tail + 1, memory_order_release);
    return true;
}

const void* aspira_spsc_front(const aspira_spsc_rb* rb) {
    if (!rb) return NULL;

    uint64_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    uint64_t head = atomic_load_explicit(&rb->head, memory_order_acquire);

    if (tail >= head) {
        return NULL;  /* Empty */
    }

    uint64_t idx = tail & rb->mask;
    return rb->buffer + idx * rb->element_size;
}

void aspira_spsc_pop_front(aspira_spsc_rb* rb) {
    if (!rb) return;

    uint64_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    atomic_store_explicit(&rb->tail, tail + 1, memory_order_release);
}

uint64_t aspira_spsc_count(const aspira_spsc_rb* rb) {
    if (!rb) return 0;
    uint64_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    uint64_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    return head - tail;
}

bool aspira_spsc_empty(const aspira_spsc_rb* rb) {
    if (!rb) return true;
    uint64_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    uint64_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    return head == tail;
}

bool aspira_spsc_full(const aspira_spsc_rb* rb) {
    if (!rb) return true;
    return aspira_spsc_count(rb) >= rb->capacity;
}

uint64_t aspira_spsc_capacity(const aspira_spsc_rb* rb) {
    if (!rb) return 0;
    return rb->capacity;
}

/* ==========================================================================
 * MPMC Ring Buffer (Vyukov Bounded Queue)
 * ========================================================================== */

bool aspira_mpmc_init(aspira_mpmc_rb* rb, uint64_t capacity, size_t element_size) {
    if (!rb || capacity == 0 || element_size == 0) {
        return false;
    }
    if (!is_power_of_two(capacity)) {
        return false;
    }

    atomic_init(&rb->enqueue_pos, 0);
    atomic_init(&rb->dequeue_pos, 0);
    rb->capacity = capacity;
    rb->mask = capacity - 1;
    rb->element_size = element_size;

    /* Slot layout: [seq (8 bytes)] [data (element_size bytes)] */
    rb->slot_size = sizeof(atomic_uint_least64_t) + element_size;
    /* Align to 8 bytes */
    rb->slot_size = (rb->slot_size + 7) & ~7UL;

    rb->buffer = (uint8_t*)calloc(capacity, rb->slot_size);
    if (!rb->buffer) {
        return false;
    }

    /* Initialize sequence counters: slot i has seq = i */
    for (uint64_t i = 0; i < capacity; i++) {
        aspira_mpmc_slot* slot = (aspira_mpmc_slot*)(rb->buffer + i * rb->slot_size);
        atomic_init(&slot->seq, i);
    }

    return true;
}

void aspira_mpmc_destroy(aspira_mpmc_rb* rb) {
    if (!rb) return;
    free(rb->buffer);
    rb->buffer = NULL;
    rb->capacity = 0;
}

bool aspira_mpmc_enqueue(aspira_mpmc_rb* rb, const void* element) {
    if (!rb || !element) return false;

    uint64_t pos;
    aspira_mpmc_slot* slot;

    for (;;) {
        pos = atomic_load_explicit(&rb->enqueue_pos, memory_order_relaxed);
        slot = (aspira_mpmc_slot*)(rb->buffer + (pos & rb->mask) * rb->slot_size);
        uint64_t seq = atomic_load_explicit(&slot->seq, memory_order_acquire);
        int64_t diff = (int64_t)seq - (int64_t)pos;

        if (diff == 0) {
            /* Slot is free, try to claim it */
            if (atomic_compare_exchange_weak_explicit(
                    &rb->enqueue_pos, &pos, pos + 1,
                    memory_order_relaxed, memory_order_relaxed)) {
                break;  /* Claimed! */
            }
        } else if (diff < 0) {
            /* Buffer is full */
            return false;
        } else {
            /* Another producer claimed this slot, retry */
            /* (CPU yield could be added here for high contention) */
        }
    }

    /* Write data to our claimed slot */
    memcpy(slot->data, element, rb->element_size);

    /* Publish: mark slot as filled */
    atomic_store_explicit(&slot->seq, pos + 1, memory_order_release);

    return true;
}

bool aspira_mpmc_dequeue(aspira_mpmc_rb* rb, void* element) {
    if (!rb || !element) return false;

    uint64_t pos;
    aspira_mpmc_slot* slot;

    for (;;) {
        pos = atomic_load_explicit(&rb->dequeue_pos, memory_order_relaxed);
        slot = (aspira_mpmc_slot*)(rb->buffer + (pos & rb->mask) * rb->slot_size);
        uint64_t seq = atomic_load_explicit(&slot->seq, memory_order_acquire);
        int64_t diff = (int64_t)seq - (int64_t)(pos + 1);

        if (diff == 0) {
            /* Slot is filled, try to claim it */
            if (atomic_compare_exchange_weak_explicit(
                    &rb->dequeue_pos, &pos, pos + 1,
                    memory_order_relaxed, memory_order_relaxed)) {
                break;  /* Claimed! */
            }
        } else if (diff < 0) {
            /* Buffer is empty */
            return false;
        } else {
            /* Another consumer claimed this slot, retry */
        }
    }

    /* Read data from our claimed slot */
    memcpy(element, slot->data, rb->element_size);

    /* Publish: mark slot as free (next cycle) */
    atomic_store_explicit(&slot->seq, pos + rb->capacity, memory_order_release);

    return true;
}

bool aspira_mpmc_enqueue_timed(aspira_mpmc_rb* rb, const void* element,
                                uint64_t timeout_ns) {
    /* Simple spin-with-backoff for timeout. In production, use
     * adaptive spinning with exponential backoff. */
    uint64_t waited = 0;
    const uint64_t backoff_ns = 10;  /* 10 ns initial backoff */

    while (waited < timeout_ns) {
        if (aspira_mpmc_enqueue(rb, element)) {
            return true;
        }
        nanosleep_spin(backoff_ns);
        waited += backoff_ns;
    }
    return false;
}

bool aspira_mpmc_dequeue_timed(aspira_mpmc_rb* rb, void* element,
                                uint64_t timeout_ns) {
    uint64_t waited = 0;
    const uint64_t backoff_ns = 10;

    while (waited < timeout_ns) {
        if (aspira_mpmc_dequeue(rb, element)) {
            return true;
        }
        nanosleep_spin(backoff_ns);
        waited += backoff_ns;
    }
    return false;
}

uint64_t aspira_mpmc_count(const aspira_mpmc_rb* rb) {
    if (!rb) return 0;
    uint64_t enq = atomic_load_explicit(&rb->enqueue_pos, memory_order_acquire);
    uint64_t deq = atomic_load_explicit(&rb->dequeue_pos, memory_order_acquire);
    return (enq >= deq) ? (enq - deq) : 0;
}

bool aspira_mpmc_empty(const aspira_mpmc_rb* rb) {
    if (!rb) return true;
    uint64_t enq = atomic_load_explicit(&rb->enqueue_pos, memory_order_acquire);
    uint64_t deq = atomic_load_explicit(&rb->dequeue_pos, memory_order_acquire);
    return enq == deq;
}

bool aspira_mpmc_full(const aspira_mpmc_rb* rb) {
    if (!rb) return true;
    uint64_t enq = atomic_load_explicit(&rb->enqueue_pos, memory_order_acquire);
    uint64_t deq = atomic_load_explicit(&rb->dequeue_pos, memory_order_acquire);
    return (enq - deq) >= rb->capacity;
}
