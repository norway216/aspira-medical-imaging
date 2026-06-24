/**
 * @file tensor.h
 * @brief NCHW tensor data structure for neural network I/O
 *
 * Tensors are the fundamental data type for the AI inference pipeline.
 * All tensor data buffers are allocated from aspira_memory_pool for
 * deterministic memory usage — no heap allocations during inference.
 *
 * Layout: NCHW (batch, channels, height, width)
 *   - innermost dimension: width (W)
 *   - element (n,c,h,w) at: data[n*stride_c + c*stride_h + h*W + w]
 */

#ifndef ASPIRA_TENSOR_H
#define ASPIRA_TENSOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief NCHW tensor with pre-computed strides
 *
 * Memory for data is either:
 *   1. Pool-allocated (owns_data=true, freed by _destroy)
 *   2. Externally owned (owns_data=false, not freed)
 */
typedef struct aspira_tensor {
    float*    data;          /* NCHW flat buffer */
    uint32_t  n;             /* Batch size (1 for inference) */
    uint32_t  c;             /* Channels */
    uint32_t  h;             /* Height (rows) */
    uint32_t  w;             /* Width (columns) */
    uint32_t  stride_c;      /* Offset per channel = H * W */
    uint32_t  stride_n;      /* Offset per batch = C * H * W */
    bool      owns_data;     /* Free data on destroy? */
    uint8_t   _pad[3];       /* Align to 32 bytes */
} aspira_tensor;

/**
 * @brief Initialize a tensor with given dimensions
 * @param t       Pre-allocated tensor struct
 * @param n,c,h,w Dimensions
 * @param data    Pre-allocated float buffer (n*c*h*w elements), or NULL
 * @param owns    If true, data will be free()d on destroy
 */
void aspira_tensor_init(aspira_tensor* t, uint32_t n, uint32_t c,
                        uint32_t h, uint32_t w, float* data, bool owns);

/**
 * @brief Destroy tensor, freeing data if owned
 */
void aspira_tensor_destroy(aspira_tensor* t);

/**
 * @brief Create a heap-allocated tensor with pool-allocated data
 * @param pool Memory pool for data buffer (can be NULL for malloc)
 * @return Heap-allocated tensor, or NULL on failure
 */
aspira_tensor* aspira_tensor_create(uint32_t n, uint32_t c,
                                     uint32_t h, uint32_t w);

/**
 * @brief Free heap-allocated tensor (calls destroy + free)
 */
void aspira_tensor_free(aspira_tensor* t);

/**
 * @brief Get total number of elements (N*C*H*W)
 */
static inline size_t aspira_tensor_elements(const aspira_tensor* t) {
    return (size_t)t->n * t->c * t->h * t->w;
}

/**
 * @brief Get total data size in bytes
 */
static inline size_t aspira_tensor_bytes(const aspira_tensor* t) {
    return aspira_tensor_elements(t) * sizeof(float);
}

/**
 * @brief Access element at (n, c, h, w) — bounds unchecked, for performance
 */
static inline float* aspira_tensor_ptr(aspira_tensor* t,
                                        uint32_t n, uint32_t c,
                                        uint32_t h, uint32_t w) {
    return t->data + ((size_t)n * t->stride_n +
                      (size_t)c * t->stride_c +
                      (size_t)h * t->w + w);
}

/**
 * @brief Const accessor
 */
static inline const float* aspira_tensor_cptr(const aspira_tensor* t,
                                               uint32_t n, uint32_t c,
                                               uint32_t h, uint32_t w) {
    return t->data + ((size_t)n * t->stride_n +
                      (size_t)c * t->stride_c +
                      (size_t)h * t->w + w);
}

/**
 * @brief Fill tensor with a constant value
 */
void aspira_tensor_fill(aspira_tensor* t, float value);

/**
 * @brief Copy data from src to dst (must have same dimensions)
 */
void aspira_tensor_copy(const aspira_tensor* src, aspira_tensor* dst);

/**
 * @brief Check if two tensors have identical dimensions
 */
bool aspira_tensor_same_shape(const aspira_tensor* a, const aspira_tensor* b);

/**
 * @brief Create a tensor that views a sub-region of another tensor
 *        (no data copy — shares the buffer)
 * @param src    Source tensor
 * @param c_start,c_end  Channel range [start, end)
 * @param h_start,h_end  Height range
 * @param w_start,w_end  Width range
 * @param view   Output view tensor (must be pre-allocated struct)
 */
void aspira_tensor_view(const aspira_tensor* src,
                         uint32_t c_start, uint32_t c_end,
                         uint32_t h_start, uint32_t h_end,
                         uint32_t w_start, uint32_t w_end,
                         aspira_tensor* view);

#ifdef __cplusplus
}
#endif

#endif /* ASPIRA_TENSOR_H */
