/**
 * @file double_buffer.c
 * @brief Lock-free ping-pong double buffer implementation
 */

#include "aspira/core/double_buffer.h"

#include <stdlib.h>
#include <string.h>

void aspira_double_buffer_init(aspira_double_buffer* db,
                                aspira_frame* front_frame,
                                aspira_frame* back_frame) {
    if (!db) return;

    memset(db, 0, sizeof(*db));
    db->buffers[0] = front_frame;
    db->buffers[1] = back_frame;
    atomic_init(&db->front, 0);
    atomic_init(&db->has_new_data, 0);
    db->owns_frames = false;
}

void aspira_double_buffer_destroy(aspira_double_buffer* db) {
    if (!db) return;

    if (db->owns_frames) {
        free(db->buffers[0]);
        free(db->buffers[1]);
    }
    memset(db, 0, sizeof(*db));
}

const aspira_frame* aspira_double_buffer_read(aspira_double_buffer* db) {
    if (!db) return NULL;

    int front = atomic_load_explicit(&db->front, memory_order_acquire);
    return db->buffers[front];
}

aspira_frame* aspira_double_buffer_write_begin(aspira_double_buffer* db) {
    if (!db) return NULL;

    int front = atomic_load_explicit(&db->front, memory_order_acquire);
    int back = 1 - front;
    return db->buffers[back];
}

void aspira_double_buffer_swap(aspira_double_buffer* db) {
    if (!db) return;

    int old_front = atomic_load_explicit(&db->front, memory_order_relaxed);
    int new_front = 1 - old_front;

    atomic_store_explicit(&db->front, new_front, memory_order_release);
    atomic_store_explicit(&db->has_new_data, 1, memory_order_release);
}

bool aspira_double_buffer_has_new_data(const aspira_double_buffer* db) {
    if (!db) return false;
    return atomic_load_explicit(&db->has_new_data, memory_order_acquire) != 0;
}

void aspira_double_buffer_mark_consumed(aspira_double_buffer* db) {
    if (!db) return;
    atomic_store_explicit(&db->has_new_data, 0, memory_order_release);
}
