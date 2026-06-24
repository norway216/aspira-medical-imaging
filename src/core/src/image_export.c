/**
 * @file image_export.c
 * @brief PGM/PPM image export implementation
 */

#include "aspira/core/image_export.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==========================================================================
 * PGM Export (Binary Grayscale)
 * ========================================================================== */

bool aspira_export_frame_pgm(const aspira_frame* frame, const char* path,
                              bool normalize) {
    if (!frame || !frame->data || !path) return false;

    FILE* f = fopen(path, "wb");
    if (!f) return false;

    uint32_t w = frame->width;
    uint32_t h = frame->height;
    size_t total = (size_t)w * h;

    /* Find min/max for normalization */
    float fmin = frame->data[0];
    float fmax = frame->data[0];
    if (normalize) {
        for (size_t i = 0; i < total; i++) {
            float v = frame->data[i];
            if (v < fmin) fmin = v;
            if (v > fmax) fmax = v;
        }
    } else {
        fmin = 0.0f;
        fmax = 1.0f;
    }

    float range = fmax - fmin;
    if (range < 1e-10f) range = 1.0f;

    /* PGM header: P5 = binary grayscale */
    fprintf(f, "P5\n%u %u\n255\n", w, h);

    /* Write pixel data (row-major) */
    for (size_t i = 0; i < total; i++) {
        float val = (frame->data[i] - fmin) / range * 255.0f;
        if (val < 0.0f) val = 0.0f;
        if (val > 255.0f) val = 255.0f;
        uint8_t pixel = (uint8_t)val;
        fwrite(&pixel, 1, 1, f);
    }

    fclose(f);
    return true;
}

bool aspira_export_tensor_pgm(const aspira_tensor* tensor, const char* path,
                               bool normalize) {
    if (!tensor || !tensor->data || !path) return false;

    FILE* f = fopen(path, "wb");
    if (!f) return false;

    uint32_t w = tensor->w;
    uint32_t h = tensor->h;
    size_t total = (size_t)w * h;

    /* Get data from first channel (N=0, C=0) */
    float fmin = tensor->data[0];
    float fmax = tensor->data[0];
    if (normalize) {
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                float v = *aspira_tensor_cptr(tensor, 0, 0, y, x);
                if (v < fmin) fmin = v;
                if (v > fmax) fmax = v;
            }
        }
    } else {
        fmin = 0.0f;
        fmax = 1.0f;
    }

    float range = fmax - fmin;
    if (range < 1e-10f) range = 1.0f;

    fprintf(f, "P5\n%u %u\n255\n", w, h);

    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            float val = (*aspira_tensor_cptr(tensor, 0, 0, y, x) - fmin) / range * 255.0f;
            if (val < 0.0f) val = 0.0f;
            if (val > 255.0f) val = 255.0f;
            uint8_t pixel = (uint8_t)val;
            fwrite(&pixel, 1, 1, f);
        }
    }

    fclose(f);
    return true;
}

bool aspira_export_mask_pgm(const float* mask, uint32_t w, uint32_t h,
                             const char* path) {
    if (!mask || !path) return false;

    FILE* f = fopen(path, "wb");
    if (!f) return false;

    fprintf(f, "P5\n%u %u\n255\n", w, h);

    size_t total = (size_t)w * h;
    for (size_t i = 0; i < total; i++) {
        uint8_t pixel = (mask[i] >= 0.5f) ? 255 : 0;
        fwrite(&pixel, 1, 1, f);
    }

    fclose(f);
    return true;
}

/* ==========================================================================
 * PPM Export (Binary RGB)
 * ========================================================================== */

bool aspira_export_frame_ppm(const aspira_frame* frame, const char* path,
                              bool normalize) {
    if (!frame || !frame->data || !path) return false;

    FILE* f = fopen(path, "wb");
    if (!f) return false;

    uint32_t w = frame->width;
    uint32_t h = frame->height;
    size_t total = (size_t)w * h;

    float fmin = frame->data[0];
    float fmax = frame->data[0];
    if (normalize) {
        for (size_t i = 0; i < total; i++) {
            float v = frame->data[i];
            if (v < fmin) fmin = v;
            if (v > fmax) fmax = v;
        }
    } else {
        fmin = 0.0f;
        fmax = 1.0f;
    }

    float range = fmax - fmin;
    if (range < 1e-10f) range = 1.0f;

    fprintf(f, "P6\n%u %u\n255\n", w, h);

    for (size_t i = 0; i < total; i++) {
        float val = (frame->data[i] - fmin) / range * 255.0f;
        if (val < 0.0f) val = 0.0f;
        if (val > 255.0f) val = 255.0f;
        uint8_t pixel = (uint8_t)val;
        /* Duplicate to R, G, B for grayscale */
        uint8_t rgb[3] = {pixel, pixel, pixel};
        fwrite(rgb, 3, 1, f);
    }

    fclose(f);
    return true;
}
