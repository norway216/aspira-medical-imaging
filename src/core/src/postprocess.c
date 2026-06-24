/**
 * @file postprocess.c
 * @brief Post-processing: threshold, morphology, confidence
 */

#include "aspira/core/postprocess.h"

#include <stdlib.h>
#include <string.h>

/* ==========================================================================
 * Segmentation Result
 * ========================================================================== */

void aspira_segmentation_result_init(aspira_segmentation_result* result) {
    if (!result) return;
    memset(result, 0, sizeof(*result));
}

void aspira_segmentation_result_destroy(aspira_segmentation_result* result) {
    if (!result) return;
    free(result->mask);
    result->mask = NULL;
}

/* ==========================================================================
 * Threshold
 * ========================================================================== */

void aspira_postprocess_threshold(const aspira_tensor* src,
                                   float threshold,
                                   aspira_tensor* dst) {
    if (!src || !src->data || !dst || !dst->data) return;

    size_t n = aspira_tensor_elements(src);
    if (n > aspira_tensor_elements(dst)) n = aspira_tensor_elements(dst);

    for (size_t i = 0; i < n; i++) {
        dst->data[i] = (src->data[i] >= threshold) ? 1.0f : 0.0f;
    }
}

/* ==========================================================================
 * Morphology: Erosion
 * ========================================================================== */

void aspira_postprocess_erode(aspira_tensor* mask, uint32_t kernel_size) {
    if (!mask || !mask->data || kernel_size < 2) return;

    uint32_t w = mask->w;
    uint32_t h = mask->h;
    int32_t radius = (int32_t)(kernel_size / 2);

    /* Allocate temporary buffer */
    float* tmp = (float*)calloc((size_t)w * h, sizeof(float));
    if (!tmp) return;

    /* Erosion: pixel becomes 0 if any neighbor is 0 */
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            /* Check if all neighbors in kernel are 1 */
            bool all_one = true;
            for (int32_t ky = -radius; ky <= radius && all_one; ky++) {
                int32_t ny = (int32_t)y + ky;
                if (ny < 0 || ny >= (int32_t)h) continue;
                for (int32_t kx = -radius; kx <= radius && all_one; kx++) {
                    int32_t nx = (int32_t)x + kx;
                    if (nx < 0 || nx >= (int32_t)w) continue;
                    if (mask->data[ny * w + nx] < 0.5f) {
                        all_one = false;
                    }
                }
            }
            tmp[y * w + x] = all_one ? 1.0f : 0.0f;
        }
    }

    memcpy(mask->data, tmp, (size_t)w * h * sizeof(float));
    free(tmp);
}

/* ==========================================================================
 * Morphology: Dilation
 * ========================================================================== */

void aspira_postprocess_dilate(aspira_tensor* mask, uint32_t kernel_size) {
    if (!mask || !mask->data || kernel_size < 2) return;

    uint32_t w = mask->w;
    uint32_t h = mask->h;
    int32_t radius = (int32_t)(kernel_size / 2);

    float* tmp = (float*)calloc((size_t)w * h, sizeof(float));
    if (!tmp) return;

    /* Dilation: pixel becomes 1 if any neighbor is 1 */
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            bool any_one = false;
            for (int32_t ky = -radius; ky <= radius && !any_one; ky++) {
                int32_t ny = (int32_t)y + ky;
                if (ny < 0 || ny >= (int32_t)h) continue;
                for (int32_t kx = -radius; kx <= radius && !any_one; kx++) {
                    int32_t nx = (int32_t)x + kx;
                    if (nx < 0 || nx >= (int32_t)w) continue;
                    if (mask->data[ny * w + nx] > 0.5f) {
                        any_one = true;
                    }
                }
            }
            tmp[y * w + x] = any_one ? 1.0f : 0.0f;
        }
    }

    memcpy(mask->data, tmp, (size_t)w * h * sizeof(float));
    free(tmp);
}

/* ==========================================================================
 * Confidence
 * ========================================================================== */

float aspira_postprocess_confidence(const aspira_tensor* prob_map) {
    if (!prob_map || !prob_map->data) return 0.0f;

    size_t n = aspira_tensor_elements(prob_map);
    if (n == 0) return 0.0f;

    double sum = 0.0;
    for (size_t i = 0; i < n; i++) {
        sum += (double)prob_map->data[i];
    }

    return (float)(sum / (double)n);
}

/* ==========================================================================
 * Full Post-processing Pipeline
 * ========================================================================== */

bool aspira_postprocess_run(const aspira_tensor* raw_output,
                             float threshold, uint32_t morph_kernel,
                             aspira_segmentation_result* result) {
    if (!raw_output || !raw_output->data || !result) return false;

    aspira_segmentation_result_init(result);

    uint32_t w = raw_output->w;
    uint32_t h = raw_output->h;

    /* Allocate mask buffer */
    result->mask = (float*)calloc((size_t)w * h, sizeof(float));
    if (!result->mask) return false;

    result->width = w;
    result->height = h;

    /* Step 1: Compute confidence from raw probabilities */
    result->confidence = aspira_postprocess_confidence(raw_output);

    /* Step 2: Threshold */
    /* Use a temporary tensor to hold the binary mask */
    float* bin_data = (float*)calloc((size_t)w * h, sizeof(float));
    if (!bin_data) {
        aspira_segmentation_result_destroy(result);
        return false;
    }

    aspira_tensor bin_tensor;
    aspira_tensor_init(&bin_tensor, 1, 1, h, w, bin_data, true);
    aspira_postprocess_threshold(raw_output, threshold, &bin_tensor);

    /* Step 3: Morphology cleanup */
    if (morph_kernel >= 2) {
        aspira_postprocess_erode(&bin_tensor, morph_kernel);
        aspira_postprocess_dilate(&bin_tensor, morph_kernel);
    }

    /* Step 4: Copy to result */
    memcpy(result->mask, bin_tensor.data, (size_t)w * h * sizeof(float));

    /* Cleanup (bin_tensor owns bin_data) */
    aspira_tensor_destroy(&bin_tensor);

    return true;
}
