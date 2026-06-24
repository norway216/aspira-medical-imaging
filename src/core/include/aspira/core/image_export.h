/**
 * @file image_export.h
 * @brief Export frame/tensor data as PGM/PPM image files
 *
 * PGM (Portable GrayMap): grayscale images, no external library needed.
 * PPM (Portable PixMap): RGB color images.
 *
 * These formats can be opened by any image viewer (eog, gimp, feh, etc.)
 * and are trivially simple to write — just a text header + binary data.
 */

#ifndef ASPIRA_IMAGE_EXPORT_H
#define ASPIRA_IMAGE_EXPORT_H

#include "frame.h"
#include "tensor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Export a frame as PGM (grayscale) image file
 *
 * Format: P5 (binary grayscale)
 *   P5\n<width> <height>\n<maxval>\n<binary data>
 *
 * @param frame   Frame to export (uses first channel only)
 * @param path    Output file path (e.g., "/tmp/frame.pgm")
 * @param normalize  If true, auto-scale data to [0..255]
 * @return true on success
 */
bool aspira_export_frame_pgm(const aspira_frame* frame, const char* path,
                              bool normalize);

/**
 * @brief Export a tensor as PGM image file
 *
 * @param tensor  Tensor to export (N=1, C=1)
 * @param path    Output file path
 * @param normalize  If true, auto-scale to [0..255]
 * @return true on success
 */
bool aspira_export_tensor_pgm(const aspira_tensor* tensor, const char* path,
                               bool normalize);

/**
 * @brief Export a segmentation mask as PGM (binary black/white)
 *
 * Mask values >= 0.5 → 255 (white), < 0.5 → 0 (black)
 */
bool aspira_export_mask_pgm(const float* mask, uint32_t w, uint32_t h,
                             const char* path);

/**
 * @brief Export a frame as PPM (RGB color) image file
 *
 * Format: P6 (binary RGB)
 *   P6\n<width> <height>\n255\n<RGB binary data>
 *
 * If frame has 1 channel, it's duplicated to R, G, B (grayscale).
 */
bool aspira_export_frame_ppm(const aspira_frame* frame, const char* path,
                              bool normalize);

#ifdef __cplusplus
}
#endif

#endif /* ASPIRA_IMAGE_EXPORT_H */
