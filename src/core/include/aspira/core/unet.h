/**
 * @file unet.h
 * @brief Lightweight U-Net CPU inference engine
 *
 * Implements a U-Net architecture optimized for CPU execution:
 *   - <6M parameters (reduced channel width)
 *   - Direct convolution (no im2col memory expansion)
 *   - All intermediate tensors pre-allocated from memory pool
 *   - Zero heap allocations during forward pass
 *
 * Architecture:
 *   Encoder: 4 blocks of [Conv+ReLU]×2 + MaxPool, 32→64→128→256 channels
 *   Bottleneck: [Conv+ReLU]×2, 512 channels
 *   Decoder: 4 blocks of UpSample+Concat+[Conv+ReLU]×2
 *   Output: Conv(1×1) + Sigmoid
 */

#ifndef ASPIRA_UNET_H
#define ASPIRA_UNET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memory_pool.h"
#include "tensor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Layer Types
 * ========================================================================== */

typedef enum {
    ASPIRA_UNET_CONV2D = 0,
    ASPIRA_UNET_RELU,
    ASPIRA_UNET_MAXPOOL2D,
    ASPIRA_UNET_UPSAMPLE,
    ASPIRA_UNET_CONCAT,
    ASPIRA_UNET_SIGMOID,
} aspira_unet_layer_type_t;

/* ==========================================================================
 * U-Net Model Configuration
 * ========================================================================== */

typedef struct {
    uint32_t input_width;          /* e.g., 256 */
    uint32_t input_height;         /* e.g., 256 */
    uint32_t input_channels;       /* e.g., 1 (grayscale) or 3 (RGB) */
    uint32_t base_channels;        /* e.g., 32 (first encoder block) */
    uint32_t num_encoder_blocks;   /* e.g., 4 */
    uint32_t num_classes;          /* e.g., 1 (binary segmentation) */
    bool     use_batch_norm;       /* reserved for future use */
} aspira_unet_config;

/* ==========================================================================
 * Convolution Layer Parameters
 * ========================================================================== */

typedef struct {
    float*   weights;         /* [oc * ic * kh * kw], row-major */
    float*   bias;            /* [oc] */
    uint32_t in_channels;
    uint32_t out_channels;
    uint32_t kernel_size;     /* 3 */
    uint32_t stride;          /* 1 */
    uint32_t padding;         /* 1 (same padding) */
} aspira_conv2d_params;

/* ==========================================================================
 * U-Net Layer Node (linked list, like signal pipeline)
 * ========================================================================== */

struct aspira_unet_layer;

typedef struct aspira_unet_layer {
    aspira_unet_layer_type_t type;
    char   name[32];

    union {
        aspira_conv2d_params conv;
        struct { uint32_t pool_size; uint32_t stride; } pool;
        struct { uint32_t scale_factor; } upsample;
        struct { int skip_index; } concat;  /* Which encoder skip to concat */
    } config;

    /* Pre-allocated output tensor (pool-allocated at model build time) */
    aspira_tensor* output;

    struct aspira_unet_layer* next;
} aspira_unet_layer;

/* ==========================================================================
 * U-Net Model
 * ========================================================================== */

typedef struct {
    /* Layer lists */
    aspira_unet_layer* encoder_head;
    aspira_unet_layer* encoder_tail;
    aspira_unet_layer* bottleneck_head;
    aspira_unet_layer* bottleneck_tail;
    aspira_unet_layer* decoder_head;
    aspira_unet_layer* decoder_tail;

    /* Skip connection buffers (pointers to encoder outputs, no copy) */
    aspira_tensor* skip_buffers[5];  /* Up to 5 skip connections */

    /* Input/output tensors */
    aspira_tensor* input_tensor;
    aspira_tensor* output_tensor;

    /* Memory pool for all intermediate tensors */
    aspira_memory_pool tensor_pool;

    /* Model configuration */
    aspira_unet_config config;

    /* Statistics */
    uint64_t total_inferences;
    uint64_t total_inference_time_ns;
    size_t   total_params;
    size_t   pool_size_bytes;
} aspira_unet_model;

/* ==========================================================================
 * Model Lifecycle API
 * ========================================================================== */

/**
 * @brief Create a U-Net model with given configuration
 *
 * Builds the full layer graph and pre-allocates all intermediate tensors
 * from an internal memory pool.
 *
 * @param config Model architecture configuration
 * @return Heap-allocated model, or NULL on failure
 */
aspira_unet_model* aspira_unet_create(const aspira_unet_config* config);

/**
 * @brief Destroy model and free all resources
 */
void aspira_unet_free(aspira_unet_model* model);

/**
 * @brief Get total number of model parameters
 */
size_t aspira_unet_param_count(const aspira_unet_model* model);

/**
 * @brief Get model pool memory usage in bytes
 */
size_t aspira_unet_pool_usage(const aspira_unet_model* model);

/* ==========================================================================
 * Weight Loading
 * ========================================================================== */

/**
 * @brief Load weights from a binary file
 *
 * Simple binary format:
 *   [magic:4B "APRN"][version:4B][num_layers:4B]
 *   Per layer:
 *     [type:4B][in_ch:4B][out_ch:4B][kernel:4B]
 *     [weights: out_ch*in_ch*kh*kw float32]
 *     [bias: out_ch float32]
 *
 * @param model  Model to load weights into (must match architecture)
 * @param path   File path to weight file
 * @return true on success
 */
bool aspira_unet_load_weights(aspira_unet_model* model, const char* path);

/* ==========================================================================
 * Forward Pass
 * ========================================================================== */

/**
 * @brief Run U-Net forward inference
 *
 * Zero heap allocations. All intermediate tensors are pre-allocated.
 * The output is available via model->output_tensor (1×C×H×W sigmoid output).
 *
 * @param model  U-Net model (with loaded weights)
 * @param input  Input tensor (must match model->config.input_channels × H × W)
 * @return true on success
 */
bool aspira_unet_forward(aspira_unet_model* model, const aspira_tensor* input);

/* ==========================================================================
 * Standalone Layer Functions (for unit testing)
 * ========================================================================== */

/**
 * @brief 2D Convolution with same padding
 *
 * Direct convolution (no im2col). Loop order optimized for CPU cache.
 * Assumes stride=1, padding=(kernel_size/2).
 *
 * @param input   Input tensor (N×C_in×H×W)
 * @param params  Conv2D parameters (weights + bias)
 * @param output  Output tensor (pre-allocated, N×C_out×H_out×W_out)
 */
void aspira_unet_conv2d(const aspira_tensor* input,
                         const aspira_conv2d_params* params,
                         aspira_tensor* output);

/**
 * @brief ReLU activation (in-place): output = max(0, input)
 */
void aspira_unet_relu(aspira_tensor* tensor);

/**
 * @brief MaxPool2D with given pool size and stride
 *
 * Output spatial dims: H_out = (H - pool_size)/stride + 1, same for W
 */
void aspira_unet_maxpool2d(const aspira_tensor* input,
                            uint32_t pool_size, uint32_t stride,
                            aspira_tensor* output);

/**
 * @brief Nearest-neighbor upsampling by scale_factor
 */
void aspira_unet_upsample(const aspira_tensor* input,
                           uint32_t scale_factor,
                           aspira_tensor* output);

/**
 * @brief Concatenate two tensors along channel dimension
 *
 * Output channels = src1->c + src2->c. Spatial dims must match.
 */
void aspira_unet_concat(const aspira_tensor* src1, const aspira_tensor* src2,
                         aspira_tensor* output);

/**
 * @brief Sigmoid activation (in-place): output = 1 / (1 + exp(-input))
 */
void aspira_unet_sigmoid(aspira_tensor* tensor);

#ifdef __cplusplus
}
#endif

#endif /* ASPIRA_UNET_H */
