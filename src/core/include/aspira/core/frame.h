/**
 * @file frame.h
 * @brief Frame data structure for medical imaging pipeline
 */

#ifndef ASPIRA_FRAME_H
#define ASPIRA_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Frame flags */
typedef enum {
    ASPIRA_FRAME_FLAG_VALID      = 1 << 0,
    ASPIRA_FRAME_FLAG_NEW        = 1 << 1,
    ASPIRA_FRAME_FLAG_PROCESSED  = 1 << 2,
    ASPIRA_FRAME_FLAG_ERROR      = 1 << 3,
    ASPIRA_FRAME_FLAG_LAST       = 1 << 4,  /* End-of-stream marker */
} aspira_frame_flags_t;

/* Imaging modality */
typedef enum {
    ASPIRA_MODALITY_ULTRASOUND = 0,
    ASPIRA_MODALITY_CT         = 1,
    ASPIRA_MODALITY_MRI        = 2,
    ASPIRA_MODALITY_XRAY       = 3,
} aspira_modality_t;

/**
 * @brief Frame - the fundamental data unit flowing through the pipeline
 *
 * Frames are allocated from memory pools and passed by pointer through
 * lock-free queues. A frame's lifecycle is:
 *   alloc from pool -> fill with data -> push to pipeline -> ... -> release to pool
 */
typedef struct aspira_frame {
    uint64_t    frame_id;        /* Monotonic frame counter */
    uint64_t    timestamp_ns;    /* Acquisition timestamp (CLOCK_MONOTONIC) */
    uint32_t    width;           /* Pixels per row (scan lines) */
    uint32_t    height;          /* Number of rows (samples per line) */
    uint32_t    channels;        /* 1 (gray) or 3 (color doppler) */
    uint32_t    depth;           /* 1 (2D) or >1 (3D volume slices) */
    size_t      data_size;       /* Total data bytes */
    float*      data;            /* Frame pixel/sample data */
    uint8_t     flags;           /* Bitfield of aspira_frame_flags_t */
    uint8_t     modality;        /* aspira_modality_t */
    float       gain;            /* Applied gain (dB) */
    float       dyn_range_db;    /* Dynamic range in dB */
    int32_t     _padding;        /* Align to 64 bytes */
    void*       _internal;       /* Internal: pool pointer for deallocation */
} aspira_frame;

/**
 * @brief Initialize a frame structure
 * @param frame Frame pointer (must not be NULL)
 * @param width Image width / scan lines
 * @param height Image height / samples per line
 * @param channels Number of channels
 * @param data Pointer to data buffer (can be NULL if unallocated)
 * @param data_size Size of data buffer in bytes
 */
void aspira_frame_init(aspira_frame* frame, uint32_t width, uint32_t height,
                       uint32_t channels, float* data, size_t data_size);

/**
 * @brief Reset frame metadata (does not free data)
 */
void aspira_frame_reset(aspira_frame* frame);

/**
 * @brief Check if frame has a specific flag set
 */
static inline bool aspira_frame_has_flag(const aspira_frame* frame, aspira_frame_flags_t flag) {
    return (frame->flags & flag) != 0;
}

/**
 * @brief Set a flag on the frame
 */
static inline void aspira_frame_set_flag(aspira_frame* frame, aspira_frame_flags_t flag) {
    frame->flags |= flag;
}

/**
 * @brief Clear a flag on the frame
 */
static inline void aspira_frame_clear_flag(aspira_frame* frame, aspira_frame_flags_t flag) {
    frame->flags &= ~flag;
}

#ifdef __cplusplus
}
#endif

#endif /* ASPIRA_FRAME_H */
