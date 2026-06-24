/**
 * @file preprocess.h
 * @brief AI inference pre-processing: ROI crop, bilinear resize, normalize
 *
 * Converts raw aspira_frame data into normalized tensor format
 * suitable for U-Net input.
 */

#ifndef ASPIRA_PREPROCESS_H
#define ASPIRA_PREPROCESS_H

#include "frame.h"
#include "tensor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Extract a rectangular ROI from a frame into a tensor
 *
 * The ROI is copied from the frame's data (row-major float array)
 * into the tensor in NCHW layout (1 channel, full ROI as H×W).
 *
 * @param frame   Source frame
 * @param roi_x   Left edge of ROI (0 = leftmost)
 * @param roi_y   Top edge of ROI (0 = topmost)
 * @param roi_w   ROI width (0 = full frame width)
 * @param roi_h   ROI height (0 = full frame height)
 * @param dst     Destination tensor (must be pre-allocated with size roi_w×roi_h)
 */
void aspira_preprocess_roi_crop(const aspira_frame* frame,
                                 uint32_t roi_x, uint32_t roi_y,
                                 uint32_t roi_w, uint32_t roi_h,
                                 aspira_tensor* dst);

/**
 * @brief Bilinear resize a tensor to target dimensions
 *
 * Supports upscaling and downscaling. Uses pre-computed interpolation
 * coordinates for speed (no per-pixel division).
 *
 * @param src   Source tensor (C×H×W)
 * @param dst   Destination tensor (must be pre-allocated, C×target_h×target_w)
 */
void aspira_preprocess_resize(const aspira_tensor* src,
                               uint32_t target_w, uint32_t target_h,
                               aspira_tensor* dst);

/**
 * @brief Normalize tensor values: output = (input - mean) / std
 *
 * In-place operation. Typical values for medical images:
 *   mean = 0.5, std = 0.5  (for [0,1] range images)
 *
 * @param tensor  Tensor to normalize (modified in-place)
 * @param mean    Mean value to subtract
 * @param std     Standard deviation to divide by
 */
void aspira_preprocess_normalize(aspira_tensor* tensor, float mean, float std);

/**
 * @brief Convenience: full pre-processing pipeline
 *
 * Runs crop → resize → normalize on a frame, producing a normalized
 * tensor ready for U-Net inference.
 *
 * @param frame      Source frame
 * @param roi_x,roi_y,roi_w,roi_h  ROI region (all 0 = full frame)
 * @param target_w,target_h  Model input dimensions (e.g., 256×256)
 * @param mean,std   Normalization parameters
 * @param output     Pre-allocated output tensor (1×1×target_h×target_w)
 * @return true on success
 */
bool aspira_preprocess_run(const aspira_frame* frame,
                            uint32_t roi_x, uint32_t roi_y,
                            uint32_t roi_w, uint32_t roi_h,
                            uint32_t target_w, uint32_t target_h,
                            float mean, float std,
                            aspira_tensor* output);

#ifdef __cplusplus
}
#endif

#endif /* ASPIRA_PREPROCESS_H */
