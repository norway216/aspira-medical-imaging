/**
 * @file tensor.c
 * @brief Tensor implementation — NCHW layout, pool-based allocation
 */

#include "aspira/core/tensor.h"

#include <stdlib.h>
#include <string.h>

void aspira_tensor_init(aspira_tensor* t, uint32_t n, uint32_t c,
                        uint32_t h, uint32_t w, float* data, bool owns) {
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->n = n;
    t->c = c;
    t->h = h;
    t->w = w;
    t->stride_c = h * w;
    t->stride_n = c * h * w;
    t->data = data;
    t->owns_data = owns;
}

void aspira_tensor_destroy(aspira_tensor* t) {
    if (!t) return;
    if (t->owns_data && t->data) {
        free(t->data);
    }
    memset(t, 0, sizeof(*t));
}

aspira_tensor* aspira_tensor_create(uint32_t n, uint32_t c,
                                     uint32_t h, uint32_t w) {
    aspira_tensor* t = (aspira_tensor*)calloc(1, sizeof(aspira_tensor));
    if (!t) return NULL;

    size_t total = (size_t)n * c * h * w;
    float* data = (float*)aligned_alloc(64, total * sizeof(float));
    if (!data) {
        free(t);
        return NULL;
    }
    /* Zero-initialize for deterministic behavior */
    memset(data, 0, total * sizeof(float));

    aspira_tensor_init(t, n, c, h, w, data, true);
    return t;
}

void aspira_tensor_free(aspira_tensor* t) {
    if (!t) return;
    aspira_tensor_destroy(t);
    free(t);
}

void aspira_tensor_fill(aspira_tensor* t, float value) {
    if (!t || !t->data) return;
    size_t n = aspira_tensor_elements(t);
    for (size_t i = 0; i < n; i++) {
        t->data[i] = value;
    }
}

void aspira_tensor_copy(const aspira_tensor* src, aspira_tensor* dst) {
    if (!src || !dst || !src->data || !dst->data) return;
    if (!aspira_tensor_same_shape(src, dst)) return;
    size_t bytes = aspira_tensor_bytes(src);
    memcpy(dst->data, src->data, bytes);
}

bool aspira_tensor_same_shape(const aspira_tensor* a, const aspira_tensor* b) {
    if (!a || !b) return false;
    return a->n == b->n && a->c == b->c && a->h == b->h && a->w == b->w;
}

void aspira_tensor_view(const aspira_tensor* src,
                         uint32_t c_start, uint32_t c_end,
                         uint32_t h_start, uint32_t h_end,
                         uint32_t w_start, uint32_t w_end,
                         aspira_tensor* view) {
    if (!src || !view) return;

    /* Clamp to valid ranges */
    if (c_end > src->c) c_end = src->c;
    if (h_end > src->h) h_end = src->h;
    if (w_end > src->w) w_end = src->w;

    uint32_t vc = (c_end > c_start) ? (c_end - c_start) : 1;
    uint32_t vh = (h_end > h_start) ? (h_end - h_start) : 1;
    uint32_t vw = (w_end > w_start) ? (w_end - w_start) : 1;

    /* View shares the source data buffer, offset to start position */
    float* view_data = src->data +
        (size_t)c_start * src->stride_c +
        (size_t)h_start * src->w +
        w_start;

    aspira_tensor_init(view, src->n, vc, vh, vw, view_data, false);
}
