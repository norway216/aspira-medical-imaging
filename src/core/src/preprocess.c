/**
 * @file preprocess.c
 * @brief Pre-processing: ROI crop, bilinear resize, normalize
 */

#include "aspira/core/preprocess.h"

#include <stdlib.h>
#include <string.h>

/* ==========================================================================
 * ROI Crop
 * ========================================================================== */

void aspira_preprocess_roi_crop(const aspira_frame* frame,
                                 uint32_t roi_x, uint32_t roi_y,
                                 uint32_t roi_w, uint32_t roi_h,
                                 aspira_tensor* dst) {
    if (!frame || !frame->data || !dst || !dst->data) return;

    /* Default: full frame if ROI is 0 */
    if (roi_w == 0) roi_w = frame->width;
    if (roi_h == 0) roi_h = frame->height;

    /* Clamp to frame boundaries */
    if (roi_x >= frame->width) roi_x = 0;
    if (roi_y >= frame->height) roi_y = 0;
    if (roi_x + roi_w > frame->width)  roi_w = frame->width - roi_x;
    if (roi_y + roi_h > frame->height) roi_h = frame->height - roi_y;

    /* Copy ROI region to tensor (row-major to NCHW, single channel) */
    float* dst_data = dst->data;
    for (uint32_t y = 0; y < roi_h; y++) {
        const float* src_row = frame->data +
            (size_t)(roi_y + y) * frame->width + roi_x;
        float* dst_row = dst_data + (size_t)y * roi_w;
        memcpy(dst_row, src_row, roi_w * sizeof(float));
    }
}

/* ==========================================================================
 * Bilinear Resize
 * ========================================================================== */

void aspira_preprocess_resize(const aspira_tensor* src,
                               uint32_t target_w, uint32_t target_h,
                               aspira_tensor* dst) {
    if (!src || !src->data || !dst || !dst->data) return;

    float scale_x = (src->w > 1) ? (float)(src->w - 1) / (float)(target_w - 1) : 0.0f;
    float scale_y = (src->h > 1) ? (float)(src->h - 1) / (float)(target_h - 1) : 0.0f;

    for (uint32_t c = 0; c < src->c && c < dst->c; c++) {
        for (uint32_t dy = 0; dy < target_h; dy++) {
            float sy = (float)dy * scale_y;
            uint32_t sy0 = (uint32_t)sy;
            uint32_t sy1 = (sy0 + 1 < src->h) ? sy0 + 1 : sy0;
            float fy = sy - (float)sy0;

            for (uint32_t dx = 0; dx < target_w; dx++) {
                float sx = (float)dx * scale_x;
                uint32_t sx0 = (uint32_t)sx;
                uint32_t sx1 = (sx0 + 1 < src->w) ? sx0 + 1 : sx0;
                float fx = sx - (float)sx0;

                /* Bilinear interpolation: 4 neighbors */
                float v00 = *aspira_tensor_cptr(src, 0, c, sy0, sx0);
                float v10 = *aspira_tensor_cptr(src, 0, c, sy0, sx1);
                float v01 = *aspira_tensor_cptr(src, 0, c, sy1, sx0);
                float v11 = *aspira_tensor_cptr(src, 0, c, sy1, sx1);

                float top = v00 * (1.0f - fx) + v10 * fx;
                float bot = v01 * (1.0f - fx) + v11 * fx;
                float val = top * (1.0f - fy) + bot * fy;

                *aspira_tensor_ptr(dst, 0, c, dy, dx) = val;
            }
        }
    }
}

/* ==========================================================================
 * Normalize
 * ========================================================================== */

void aspira_preprocess_normalize(aspira_tensor* tensor, float mean, float std) {
    if (!tensor || !tensor->data) return;

    float inv_std = 1.0f / std;
    size_t n = aspira_tensor_elements(tensor);

    for (size_t i = 0; i < n; i++) {
        tensor->data[i] = (tensor->data[i] - mean) * inv_std;
    }
}

/* ==========================================================================
 * Full Pre-processing Pipeline
 * ========================================================================== */

bool aspira_preprocess_run(const aspira_frame* frame,
                            uint32_t roi_x, uint32_t roi_y,
                            uint32_t roi_w, uint32_t roi_h,
                            uint32_t target_w, uint32_t target_h,
                            float mean, float std,
                            aspira_tensor* output) {
    if (!frame || !frame->data || !output) return false;

    uint32_t crop_w = (roi_w > 0) ? roi_w : frame->width;
    uint32_t crop_h = (roi_h > 0) ? roi_h : frame->height;

    /* Allocate temporary tensor for cropped data */
    aspira_tensor* cropped = aspira_tensor_create(1, 1, crop_h, crop_w);
    if (!cropped) return false;

    /* Step 1: Crop */
    aspira_preprocess_roi_crop(frame, roi_x, roi_y, roi_w, roi_h, cropped);

    /* Step 2: Resize (if needed) */
    if (crop_w == target_w && crop_h == target_h) {
        /* No resize needed — copy directly to output */
        aspira_tensor_copy(cropped, output);
    } else {
        aspira_tensor* resized = aspira_tensor_create(1, 1, target_h, target_w);
        if (!resized) {
            aspira_tensor_free(cropped);
            return false;
        }
        aspira_preprocess_resize(cropped, target_w, target_h, resized);
        aspira_tensor_copy(resized, output);
        aspira_tensor_free(resized);
    }

    /* Step 3: Normalize */
    aspira_preprocess_normalize(output, mean, std);

    aspira_tensor_free(cropped);
    return true;
}
