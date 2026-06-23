/**
 * @file double_buffer.h
 * @brief Lock-free ping-pong double buffer for frame display
 *
 * Writer writes to the back buffer, reader reads from the front buffer.
 * The swap operation atomically exchanges front and back, providing
 * the reader with a consistent view of the latest frame.
 *
 * Usage pattern:
 *   Writer: aspira_double_buffer_write_begin() -> fill data -> write_commit()
 *   Reader: aspira_double_buffer_read_begin() -> read data -> read_done()
 *   Or simpler: aspira_double_buffer_swap() for single-producer scenarios
 */

#ifndef ASPIRA_DOUBLE_BUFFER_H
#define ASPIRA_DOUBLE_BUFFER_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Double buffer for lock-free frame swapping
 */
typedef struct aspira_double_buffer {
    aspira_frame* buffers[2];     /* Two pre-allocated frame buffers */
    atomic_int    front;          /* Index of current read buffer (0 or 1) */
    atomic_int    has_new_data;   /* 1 if back buffer has been swapped in */
    bool          owns_frames;    /* Whether we should free the frames */
} aspira_double_buffer;

/**
 * @brief Initialize double buffer with pre-allocated frames
 * @param db Pointer to uninitialized double buffer
 * @param front_frame Frame for the front slot
 * @param back_frame Frame for the back slot
 */
void aspira_double_buffer_init(aspira_double_buffer* db,
                                aspira_frame* front_frame,
                                aspira_frame* back_frame);

/**
 * @brief Destroy double buffer (frees frames if owned)
 */
void aspira_double_buffer_destroy(aspira_double_buffer* db);

/**
 * @brief Get the current front (read) buffer
 * @return Const pointer to current read frame (never NULL after init)
 */
const aspira_frame* aspira_double_buffer_read(aspira_double_buffer* db);

/**
 * @brief Get the back (write) buffer for modification
 * @return Mutable pointer to back buffer (never NULL after init)
 */
aspira_frame* aspira_double_buffer_write_begin(aspira_double_buffer* db);

/**
 * @brief Swap front and back buffers atomically
 *
 * After this call, what was the back buffer becomes the front,
 * making it visible to readers. Thread-safe for single writer.
 */
void aspira_double_buffer_swap(aspira_double_buffer* db);

/**
 * @brief Check if new data is available since last read
 */
bool aspira_double_buffer_has_new_data(const aspira_double_buffer* db);

/**
 * @brief Mark that reader has consumed the current data
 */
void aspira_double_buffer_mark_consumed(aspira_double_buffer* db);

#ifdef __cplusplus
}
#endif

#endif /* ASPIRA_DOUBLE_BUFFER_H */
