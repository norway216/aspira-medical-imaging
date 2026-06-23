/**
 * @file frame.c
 * @brief Frame data structure implementation
 */

#include "aspira/core/frame.h"

#include <string.h>

void aspira_frame_init(aspira_frame* frame, uint32_t width, uint32_t height,
                       uint32_t channels, float* data, size_t data_size) {
    if (!frame) return;

    memset(frame, 0, sizeof(*frame));
    frame->width = width;
    frame->height = height;
    frame->channels = channels;
    frame->depth = 1;
    frame->data = data;
    frame->data_size = data_size;
    frame->flags = 0;
    frame->modality = ASPIRA_MODALITY_ULTRASOUND;
    frame->gain = 0.0f;
    frame->dyn_range_db = 60.0f;
}

void aspira_frame_reset(aspira_frame* frame) {
    if (!frame) return;
    frame->frame_id = 0;
    frame->timestamp_ns = 0;
    frame->flags = 0;
    frame->gain = 0.0f;
    if (frame->data && frame->data_size > 0) {
        memset(frame->data, 0, frame->data_size);
    }
}
