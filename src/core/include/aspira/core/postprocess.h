/**
 * @file postprocess.h
 * @brief AI inference post-processing: threshold, erosion, dilation
 *
 * Converts raw U-Net sigmoid output (probability map) into a clean
 * binary segmentation mask suitable for overlay on the original frame.
 */

#ifndef ASPIRA_POSTPROCESS_H
#define ASPIRA_POSTPROCESS_H

#include "tensor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Segmentation result structure
 */
typedef struct {
    float*    mask;             /* Flat [H x W] binary mask (0.0 or 1.0) */
    uint32_t  width;            /* Mask width */
    uint32_t  height;           /* Mask height */
    float     confidence;       /* Mean probability across all pixels */
    uint32_t  roi_x, roi_y;     /* ROI origin in original frame coords */
    uint32_t  roi_w, roi_h;     /* ROI dimensions */
    uint64_t  timestamp_ns;     /* Frame timestamp */
    uint64_t  inference_us;     /* Inference duration in microseconds */
} aspira_segmentation_result;

/**
 * @brief Initialize a segmentation result structure
 */
void aspira_segmentation_result_init(aspira_segmentation_result* result);

/**
 * @brief Destroy result, freeing mask data
 */
void aspira_segmentation_result_destroy(aspira_segmentation_result* result);

/**
 * @brief Apply threshold to convert probability map to binary mask
 *
 * output[i] = (input[i] >= threshold) ? 1.0f : 0.0f
 *
 * @param src        Input probability tensor (C×H×W, values in [0,1])
 * @param threshold  Decision boundary (typically 0.5)
 * @param dst        Output binary tensor (same dimensions as src)
 */
void aspira_postprocess_threshold(const aspira_tensor* src,
                                   float threshold,
                                   aspira_tensor* dst);

/**
 * @brief Binary erosion using a square structuring element
 *
 * Erodes white (1.0) regions. Operates in-place with a temporary buffer.
 *
 * @param mask         Binary mask tensor (modified in-place)
 * @param kernel_size  Size of square structuring element (3 = 3x3)
 */
void aspira_postprocess_erode(aspira_tensor* mask, uint32_t kernel_size);

/**
 * @brief Binary dilation using a square structuring element
 *
 * Dilates white (1.0) regions. Operates in-place with a temporary buffer.
 *
 * @param mask         Binary mask tensor (modified in-place)
 * @param kernel_size  Size of square structuring element (3 = 3x3)
 */
void aspira_postprocess_dilate(aspira_tensor* mask, uint32_t kernel_size);

/**
 * @brief Compute mean confidence from probability map
 */
float aspira_postprocess_confidence(const aspira_tensor* prob_map);

/**
 * @brief Convenience: full post-processing pipeline
 *
 * Runs threshold → (optional erode) → (optional dilate) → confidence
 *
 * @param raw_output   Raw U-Net sigmoid output (1×1×H×W)
 * @param threshold    Decision boundary (0.5)
 * @param morph_kernel Morphology kernel size (0 = skip, 3 = 3×3)
 * @param result       Output segmentation result (mask allocated internally)
 * @return true on success
 */
bool aspira_postprocess_run(const aspira_tensor* raw_output,
                             float threshold, uint32_t morph_kernel,
                             aspira_segmentation_result* result);

#ifdef __cplusplus
}
#endif

#endif /* ASPIRA_POSTPROCESS_H */
