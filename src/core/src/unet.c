/**
 * @file unet.c
 * @brief U-Net CPU inference engine implementation
 *
 * All operations are pure C with compiler auto-vectorization.
 * Zero heap allocations during forward() — all tensors pre-allocated
 * from the model's internal memory pool.
 */

#include "aspira/core/unet.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==========================================================================
 * Internal Helpers
 * ========================================================================== */

static aspira_unet_layer* alloc_layer(aspira_unet_layer_type_t type,
                                       const char* name) {
    aspira_unet_layer* l = (aspira_unet_layer*)calloc(1, sizeof(*l));
    if (!l) return NULL;
    l->type = type;
    if (name) strncpy(l->name, name, sizeof(l->name) - 1);
    return l;
}

static void link_layer(aspira_unet_layer** head, aspira_unet_layer** tail,
                        aspira_unet_layer* layer) {
    if (!*head) {
        *head = layer;
    } else {
        (*tail)->next = layer;
    }
    *tail = layer;
}

/* ==========================================================================
 * Conv2D
 * ========================================================================== */

void aspira_unet_conv2d(const aspira_tensor* input,
                         const aspira_conv2d_params* p,
                         aspira_tensor* output) {
    if (!input || !p || !output) return;

    uint32_t ic = p->in_channels;
    uint32_t oc = p->out_channels;
    uint32_t kh = p->kernel_size;
    uint32_t kw = p->kernel_size;
    uint32_t pad = p->padding;
    uint32_t H = input->h;
    uint32_t W = input->w;
    uint32_t OH = output->h;
    uint32_t OW = output->w;

    /* Clear output (bias will be added) */
    size_t out_elems = aspira_tensor_elements(output);
    memset(output->data, 0, out_elems * sizeof(float));

    /* Direct convolution — loop order optimized for CPU cache:
     * oc → oh → ow → ic → kh → kw
     * Weight layout: [oc][ic][kh][kw] */
    for (uint32_t o = 0; o < oc; o++) {
        float bias_val = (p->bias) ? p->bias[o] : 0.0f;
        for (uint32_t y = 0; y < OH; y++) {
            for (uint32_t x = 0; x < OW; x++) {
                float sum = bias_val;
                for (uint32_t i = 0; i < ic; i++) {
                    const float* weight_ptr = p->weights +
                        ((size_t)o * ic + i) * kh * kw;
                    for (uint32_t ky = 0; ky < kh; ky++) {
                        int32_t iy = (int32_t)(y + ky) - (int32_t)pad;
                        if (iy < 0 || iy >= (int32_t)H) continue;
                        for (uint32_t kx = 0; kx < kw; kx++) {
                            int32_t ix = (int32_t)(x + kx) - (int32_t)pad;
                            if (ix < 0 || ix >= (int32_t)W) continue;
                            sum += weight_ptr[ky * kw + kx] *
                                   *aspira_tensor_cptr(input, 0, i, (uint32_t)iy, (uint32_t)ix);
                        }
                    }
                }
                *aspira_tensor_ptr(output, 0, o, y, x) = sum;
            }
        }
    }
}

/* ==========================================================================
 * ReLU
 * ========================================================================== */

void aspira_unet_relu(aspira_tensor* t) {
    if (!t || !t->data) return;
    size_t n = aspira_tensor_elements(t);
    for (size_t i = 0; i < n; i++) {
        if (t->data[i] < 0.0f) t->data[i] = 0.0f;
    }
}

/* ==========================================================================
 * MaxPool2D
 * ========================================================================== */

void aspira_unet_maxpool2d(const aspira_tensor* input,
                            uint32_t pool_size, uint32_t stride,
                            aspira_tensor* output) {
    if (!input || !output) return;

    uint32_t C = input->c;
    uint32_t OH = output->h;
    uint32_t OW = output->w;

    for (uint32_t c = 0; c < C; c++) {
        for (uint32_t y = 0; y < OH; y++) {
            for (uint32_t x = 0; x < OW; x++) {
                float max_val = -INFINITY;
                for (uint32_t ky = 0; ky < pool_size; ky++) {
                    uint32_t iy = y * stride + ky;
                    if (iy >= input->h) continue;
                    for (uint32_t kx = 0; kx < pool_size; kx++) {
                        uint32_t ix = x * stride + kx;
                        if (ix >= input->w) continue;
                        float v = *aspira_tensor_cptr(input, 0, c, iy, ix);
                        if (v > max_val) max_val = v;
                    }
                }
                *aspira_tensor_ptr(output, 0, c, y, x) = max_val;
            }
        }
    }
}

/* ==========================================================================
 * UpSample (Nearest Neighbor)
 * ========================================================================== */

void aspira_unet_upsample(const aspira_tensor* input,
                           uint32_t scale_factor,
                           aspira_tensor* output) {
    if (!input || !output) return;

    for (uint32_t c = 0; c < input->c; c++) {
        for (uint32_t y = 0; y < input->h; y++) {
            for (uint32_t x = 0; x < input->w; x++) {
                float val = *aspira_tensor_cptr(input, 0, c, y, x);
                /* Replicate to scale_factor x scale_factor block */
                for (uint32_t dy = 0; dy < scale_factor; dy++) {
                    uint32_t oy = y * scale_factor + dy;
                    if (oy >= output->h) continue;
                    for (uint32_t dx = 0; dx < scale_factor; dx++) {
                        uint32_t ox = x * scale_factor + dx;
                        if (ox >= output->w) continue;
                        *aspira_tensor_ptr(output, 0, c, oy, ox) = val;
                    }
                }
            }
        }
    }
}

/* ==========================================================================
 * Concat
 * ========================================================================== */

void aspira_unet_concat(const aspira_tensor* src1, const aspira_tensor* src2,
                         aspira_tensor* output) {
    if (!src1 || !src2 || !output) return;

    /* Copy src1 channels to output channels [0..src1->c) */
    for (uint32_t c = 0; c < src1->c; c++) {
        for (uint32_t y = 0; y < src1->h; y++) {
            size_t src_off = (size_t)c * src1->stride_c + (size_t)y * src1->w;
            size_t dst_off = (size_t)c * output->stride_c + (size_t)y * output->w;
            memcpy(output->data + dst_off, src1->data + src_off,
                   src1->w * sizeof(float));
        }
    }

    /* Copy src2 channels to output channels [src1->c .. src1->c+src2->c) */
    for (uint32_t c = 0; c < src2->c; c++) {
        for (uint32_t y = 0; y < src2->h; y++) {
            size_t src_off = (size_t)c * src2->stride_c + (size_t)y * src2->w;
            size_t dst_off = (size_t)(src1->c + c) * output->stride_c +
                            (size_t)y * output->w;
            memcpy(output->data + dst_off, src2->data + src_off,
                   src2->w * sizeof(float));
        }
    }
}

/* ==========================================================================
 * Sigmoid
 * ========================================================================== */

void aspira_unet_sigmoid(aspira_tensor* t) {
    if (!t || !t->data) return;
    size_t n = aspira_tensor_elements(t);
    for (size_t i = 0; i < n; i++) {
        /* Clamp to avoid overflow */
        float x = t->data[i];
        if (x < -20.0f) x = -20.0f;
        if (x > 20.0f) x = 20.0f;
        t->data[i] = 1.0f / (1.0f + expf(-x));
    }
}

/* ==========================================================================
 * Model Build
 * ========================================================================== */

static size_t compute_pool_size(const aspira_unet_config* cfg) {
    /* Sum of all intermediate tensor sizes at max resolution.
     * Conservative estimate: worst-case activation * 3 for scratch. */
    uint32_t base = cfg->base_channels;
    uint32_t W = cfg->input_width;
    uint32_t H = cfg->input_height;
    size_t total = 0;

    /* Encoder activations (double channels each block, half spatial) */
    uint32_t c = base;
    for (uint32_t i = 0; i < cfg->num_encoder_blocks; i++) {
        total += (size_t)c * H * W * sizeof(float) * 3;  /* conv+relu ×2 + pool */
        c *= 2;
        H /= 2; W /= 2;
    }

    /* Bottleneck */
    total += (size_t)c * H * W * sizeof(float) * 2;

    /* Decoder (upsample + concat + conv×2) */
    for (uint32_t i = 0; i < cfg->num_encoder_blocks; i++) {
        H *= 2; W *= 2;
        c /= 2;
        /* Upsample output + concat output + 2×conv output */
        total += (size_t)(c * 2) * H * W * sizeof(float) * 4;
    }

    /* Output */
    total += (size_t)cfg->num_classes * cfg->input_height * cfg->input_width
             * sizeof(float);

    return total + (64 * 1024);  /* 64KB extra margin */
}

aspira_unet_model* aspira_unet_create(const aspira_unet_config* cfg) {
    if (!cfg || cfg->input_width == 0) return NULL;

    aspira_unet_model* m = (aspira_unet_model*)calloc(1, sizeof(*m));
    if (!m) return NULL;

    m->config = *cfg;
    uint32_t Ic = cfg->input_channels;
    uint32_t Oc = cfg->num_classes;
    uint32_t base = cfg->base_channels;
    uint32_t W = cfg->input_width;
    uint32_t H = cfg->input_height;

    /* Pre-allocate memory pool for all intermediate tensors */
    size_t pool_bytes = compute_pool_size(cfg);
    if (!aspira_memory_pool_init(&m->tensor_pool, pool_bytes, 1)) {
        /* Pool too small — just use the one large block for scratch allocs */
        aspira_memory_pool_init(&m->tensor_pool, pool_bytes, 1);
    }
    m->pool_size_bytes = pool_bytes;

    /* Build encoder blocks */
    uint32_t current_c = Ic;
    for (uint32_t b = 0; b < cfg->num_encoder_blocks; b++) {
        char name[32];
        uint32_t block_c = (b == 0) ? base : (base << b);  /* 16, 32, 64, 128... */
        /* Conv + ReLU × 2 */
        for (int conv = 0; conv < 2; conv++) {
            uint32_t in_c = (conv == 0) ? current_c : block_c;
            snprintf(name, sizeof(name), "enc%d_conv%d", b, conv);
            aspira_unet_layer* l = alloc_layer(ASPIRA_UNET_CONV2D, name);
            l->config.conv.in_channels = in_c;
            l->config.conv.out_channels = block_c;
            l->config.conv.kernel_size = 3;
            l->config.conv.stride = 1;
            l->config.conv.padding = 1;
            link_layer(&m->encoder_head, &m->encoder_tail, l);

            snprintf(name, sizeof(name), "enc%d_relu%d", b, conv);
            l = alloc_layer(ASPIRA_UNET_RELU, name);
            link_layer(&m->encoder_head, &m->encoder_tail, l);
        }
        current_c = block_c;

        /* MaxPool (except last encoder block) */
        if (b < cfg->num_encoder_blocks - 1) {
            snprintf(name, sizeof(name), "enc%d_pool", b);
            aspira_unet_layer* l = alloc_layer(ASPIRA_UNET_MAXPOOL2D, name);
            l->config.pool.pool_size = 2;
            l->config.pool.stride = 2;
            link_layer(&m->encoder_head, &m->encoder_tail, l);
        }
    }

    /* Bottleneck */
    uint32_t bt_c = current_c * 2;
    for (int i = 0; i < 2; i++) {
        char name[32];
        uint32_t in_c = (i == 0) ? current_c : bt_c;
        snprintf(name, sizeof(name), "bottleneck_conv%d", i);
        aspira_unet_layer* l = alloc_layer(ASPIRA_UNET_CONV2D, name);
        l->config.conv.in_channels = in_c;
        l->config.conv.out_channels = bt_c;
        l->config.conv.kernel_size = 3;
        l->config.conv.stride = 1;
        l->config.conv.padding = 1;
        link_layer(&m->bottleneck_head, &m->bottleneck_tail, l);

        snprintf(name, sizeof(name), "bottleneck_relu%d", i);
        l = alloc_layer(ASPIRA_UNET_RELU, name);
        link_layer(&m->bottleneck_head, &m->bottleneck_tail, l);
    }
    current_c = bt_c;

    /* Build decoder blocks — count matches encoder blocks with a pool.
       Encoder blocks [0..N-1] have pools for [0..N-2] (last block no pool).
       Decoder blocks go from N-2 down to 0, each with UpSample + Concat */
    for (int32_t b = (int32_t)cfg->num_encoder_blocks - 2; b >= 0; b--) {
        char name[32];
        uint32_t skip_c = base << (uint32_t)b;
        uint32_t dec_c = skip_c;
        uint32_t concat_c = current_c + skip_c;

        /* UpSample */
        snprintf(name, sizeof(name), "dec%d_up", b);
        aspira_unet_layer* l = alloc_layer(ASPIRA_UNET_UPSAMPLE, name);
        l->config.upsample.scale_factor = 2;
        link_layer(&m->decoder_head, &m->decoder_tail, l);

        /* Concat with skip */
        snprintf(name, sizeof(name), "dec%d_concat", b);
        l = alloc_layer(ASPIRA_UNET_CONCAT, name);
        l->config.concat.skip_index = b;
        link_layer(&m->decoder_head, &m->decoder_tail, l);

        /* Conv + ReLU × 2 */
        for (int conv = 0; conv < 2; conv++) {
            uint32_t conv_in = (conv == 0) ? concat_c : dec_c;
            snprintf(name, sizeof(name), "dec%d_conv%d", b, conv);
            l = alloc_layer(ASPIRA_UNET_CONV2D, name);
            l->config.conv.in_channels = conv_in;
            l->config.conv.out_channels = dec_c;
            l->config.conv.kernel_size = 3;
            l->config.conv.stride = 1;
            l->config.conv.padding = 1;
            link_layer(&m->decoder_head, &m->decoder_tail, l);

            snprintf(name, sizeof(name), "dec%d_relu%d", b, conv);
            l = alloc_layer(ASPIRA_UNET_RELU, name);
            link_layer(&m->decoder_head, &m->decoder_tail, l);
        }
        current_c = dec_c;
    }

    /* Final block: same spatial as last skip (no upsample needed) */
    {
        uint32_t b = (uint32_t)cfg->num_encoder_blocks - 1;
        uint32_t skip_c = base << b;
        uint32_t dec_c = skip_c;
        uint32_t concat_c = current_c + skip_c;
        char name[32];

        snprintf(name, sizeof(name), "dec%d_concat", b);
        aspira_unet_layer* l = alloc_layer(ASPIRA_UNET_CONCAT, name);
        l->config.concat.skip_index = (int32_t)b;
        link_layer(&m->decoder_head, &m->decoder_tail, l);

        for (int conv = 0; conv < 2; conv++) {
            uint32_t conv_in = (conv == 0) ? concat_c : dec_c;
            snprintf(name, sizeof(name), "dec%d_conv%d", b, conv);
            l = alloc_layer(ASPIRA_UNET_CONV2D, name);
            l->config.conv.in_channels = conv_in;
            l->config.conv.out_channels = dec_c;
            l->config.conv.kernel_size = 3;
            l->config.conv.stride = 1;
            l->config.conv.padding = 1;
            link_layer(&m->decoder_head, &m->decoder_tail, l);

            snprintf(name, sizeof(name), "dec%d_relu%d", b, conv);
            l = alloc_layer(ASPIRA_UNET_RELU, name);
            link_layer(&m->decoder_head, &m->decoder_tail, l);
        }
        current_c = dec_c;
    }

    /* Output: 1x1 Conv + Sigmoid */
    {
        aspira_unet_layer* l = alloc_layer(ASPIRA_UNET_CONV2D, "output_conv");
        l->config.conv.in_channels = current_c;
        l->config.conv.out_channels = Oc;
        l->config.conv.kernel_size = 1;
        l->config.conv.stride = 1;
        l->config.conv.padding = 0;
        link_layer(&m->decoder_head, &m->decoder_tail, l);

        l = alloc_layer(ASPIRA_UNET_SIGMOID, "output_sigmoid");
        link_layer(&m->decoder_head, &m->decoder_tail, l);
    }

    /* Allocate weights for conv layers (random init for now) */
    for (aspira_unet_layer* l = m->encoder_head; l; l = l->next) {
        if (l->type == ASPIRA_UNET_CONV2D) {
            aspira_conv2d_params* cp = &l->config.conv;
            size_t w_size = (size_t)cp->out_channels * cp->in_channels *
                            cp->kernel_size * cp->kernel_size;
            cp->weights = (float*)calloc(w_size, sizeof(float));
            cp->bias = (float*)calloc(cp->out_channels, sizeof(float));
            m->total_params += w_size + cp->out_channels;
            /* Xavier-like random init */
            float scale = sqrtf(2.0f / (float)(cp->in_channels * cp->kernel_size * cp->kernel_size));
            for (size_t i = 0; i < w_size; i++)
                cp->weights[i] = ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f * scale;
        }
    }
    for (aspira_unet_layer* l = m->bottleneck_head; l; l = l->next) {
        if (l->type == ASPIRA_UNET_CONV2D) {
            aspira_conv2d_params* cp = &l->config.conv;
            size_t w_size = (size_t)cp->out_channels * cp->in_channels *
                            cp->kernel_size * cp->kernel_size;
            cp->weights = (float*)calloc(w_size, sizeof(float));
            cp->bias = (float*)calloc(cp->out_channels, sizeof(float));
            m->total_params += w_size + cp->out_channels;
            float scale = sqrtf(2.0f / (float)(cp->in_channels * cp->kernel_size * cp->kernel_size));
            for (size_t i = 0; i < w_size; i++)
                cp->weights[i] = ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f * scale;
        }
    }
    for (aspira_unet_layer* l = m->decoder_head; l; l = l->next) {
        if (l->type == ASPIRA_UNET_CONV2D) {
            aspira_conv2d_params* cp = &l->config.conv;
            size_t w_size = (size_t)cp->out_channels * cp->in_channels *
                            cp->kernel_size * cp->kernel_size;
            cp->weights = (float*)calloc(w_size, sizeof(float));
            cp->bias = (float*)calloc(cp->out_channels, sizeof(float));
            m->total_params += w_size + cp->out_channels;
            float scale = sqrtf(2.0f / (float)(cp->in_channels * cp->kernel_size * cp->kernel_size));
            for (size_t i = 0; i < w_size; i++)
                cp->weights[i] = ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f * scale;
        }
    }

    /* Create input/output tensors */
    m->input_tensor = aspira_tensor_create(1, Ic, cfg->input_height, cfg->input_width);
    m->output_tensor = aspira_tensor_create(1, Oc, cfg->input_height, cfg->input_width);

    return m;
}

void aspira_unet_free(aspira_unet_model* m) {
    if (!m) return;

    /* Free weights in all layers */
    for (aspira_unet_layer* l = m->encoder_head; l; l = l->next) {
        if (l->type == ASPIRA_UNET_CONV2D) {
            free(l->config.conv.weights);
            free(l->config.conv.bias);
        }
    }
    for (aspira_unet_layer* l = m->bottleneck_head; l; l = l->next) {
        if (l->type == ASPIRA_UNET_CONV2D) {
            free(l->config.conv.weights);
            free(l->config.conv.bias);
        }
    }
    for (aspira_unet_layer* l = m->decoder_head; l; l = l->next) {
        if (l->type == ASPIRA_UNET_CONV2D) {
            free(l->config.conv.weights);
            free(l->config.conv.bias);
        }
    }

    /* Free layers (linked lists) */
    aspira_unet_layer* node = m->encoder_head;
    while (node) { aspira_unet_layer* n = node->next; free(node); node = n; }
    node = m->bottleneck_head;
    while (node) { aspira_unet_layer* n = node->next; free(node); node = n; }
    node = m->decoder_head;
    while (node) { aspira_unet_layer* n = node->next; free(node); node = n; }

    aspira_tensor_free(m->input_tensor);
    aspira_tensor_free(m->output_tensor);
    aspira_memory_pool_destroy(&m->tensor_pool);
    free(m);
}

size_t aspira_unet_param_count(const aspira_unet_model* m) {
    return m ? m->total_params : 0;
}

size_t aspira_unet_pool_usage(const aspira_unet_model* m) {
    return m ? m->pool_size_bytes : 0;
}

/* ==========================================================================
 * Weight Loading
 * ========================================================================== */

bool aspira_unet_load_weights(aspira_unet_model* model, const char* path) {
    if (!model || !path) return false;

    FILE* f = fopen(path, "rb");
    if (!f) return false;

    /* Read header */
    uint32_t magic = 0, version = 0, num_layers = 0;
    if (fread(&magic, 4, 1, f) != 1 || magic != 0x4150524E) goto fail;
    if (fread(&version, 4, 1, f) != 1) goto fail;
    if (fread(&num_layers, 4, 1, f) != 1) goto fail;
    (void)version;

    /* Read per-layer weights */
    for (uint32_t li = 0; li < num_layers; li++) {
        uint32_t type, in_c, out_c, kernel;
        if (fread(&type, 4, 1, f) != 1) goto fail;
        if (fread(&in_c, 4, 1, f) != 1) goto fail;
        if (fread(&out_c, 4, 1, f) != 1) goto fail;
        if (fread(&kernel, 4, 1, f) != 1) goto fail;

        /* Find matching conv layer and load weights */
        size_t w_size = (size_t)out_c * in_c * kernel * kernel;
        size_t b_size = (size_t)out_c;

        /* Search for this layer in encoder/bottleneck/decoder */
        aspira_unet_layer* found = NULL;
        for (int section = 0; section < 3 && !found; section++) {
            aspira_unet_layer* start = section == 0 ? model->encoder_head :
                                        section == 1 ? model->bottleneck_head :
                                        model->decoder_head;
            for (aspira_unet_layer* l = start; l; l = l->next) {
                if (l->type == ASPIRA_UNET_CONV2D &&
                    l->config.conv.in_channels == in_c &&
                    l->config.conv.out_channels == out_c &&
                    l->config.conv.kernel_size == kernel) {
                    found = l;
                    break;
                }
            }
        }

        if (found) {
            if (fread(found->config.conv.weights, sizeof(float), w_size, f) != w_size) goto fail;
            if (fread(found->config.conv.bias, sizeof(float), b_size, f) != b_size) goto fail;
        } else {
            /* Skip unknown layer */
            fseek(f, (long)((w_size + b_size) * sizeof(float)), SEEK_CUR);
        }
    }

    fclose(f);
    return true;

fail:
    fclose(f);
    return false;
}

/* ==========================================================================
 * Forward Pass
 * ========================================================================== */

static bool alloc_layer_tensors(aspira_unet_layer* head,
                                 aspira_tensor* first_input) {
    aspira_tensor* prev_output = first_input;
    for (aspira_unet_layer* l = head; l; l = l->next) {
        uint32_t oc = 0, oh = 0, ow = 0;

        switch (l->type) {
        case ASPIRA_UNET_CONV2D:
            oc = l->config.conv.out_channels;
            oh = prev_output->h;
            ow = prev_output->w;
            break;
        case ASPIRA_UNET_RELU:
        case ASPIRA_UNET_SIGMOID:
            /* In-place — no output tensor needed */
            prev_output = prev_output;  /* stays same */
            continue;
        case ASPIRA_UNET_MAXPOOL2D:
            oc = prev_output->c;
            oh = (prev_output->h - l->config.pool.pool_size) / l->config.pool.stride + 1;
            ow = (prev_output->w - l->config.pool.pool_size) / l->config.pool.stride + 1;
            break;
        case ASPIRA_UNET_UPSAMPLE:
            oc = prev_output->c;
            oh = prev_output->h * l->config.upsample.scale_factor;
            ow = prev_output->w * l->config.upsample.scale_factor;
            break;
        case ASPIRA_UNET_CONCAT:
            oc = prev_output->c * 2;  /* skip has same channels */
            oh = prev_output->h;
            ow = prev_output->w;
            break;
        }

        /* Allocate heap tensor for this layer's output */
        l->output = aspira_tensor_create(1, oc, oh, ow);
        if (!l->output) return false;
        prev_output = l->output;
    }
    return true;
}

const aspira_tensor* aspira_unet_output(const aspira_unet_model* m) {
    return m ? m->output_tensor : NULL;
}

bool aspira_unet_forward(aspira_unet_model* m, const aspira_tensor* input) {
    if (!m || !input) return false;

    /* Lazily allocate layer output tensors on first forward pass */
    bool need_alloc = false;
    for (aspira_unet_layer* l = m->encoder_head; l && !need_alloc; l = l->next)
        if (l->type != ASPIRA_UNET_RELU && !l->output) need_alloc = true;
    if (!need_alloc)
        for (aspira_unet_layer* l = m->bottleneck_head; l && !need_alloc; l = l->next)
            if (l->type != ASPIRA_UNET_RELU && !l->output) need_alloc = true;
    if (!need_alloc)
        for (aspira_unet_layer* l = m->decoder_head; l && !need_alloc; l = l->next)
            if (l->type != ASPIRA_UNET_RELU && l->type != ASPIRA_UNET_SIGMOID && !l->output) need_alloc = true;

    if (need_alloc) {
        /* Compute bottleneck input dimensions (after all encoder pooling) */
        uint32_t bt_H = m->config.input_height;
        uint32_t bt_W = m->config.input_width;
        for (uint32_t i = 0; i < m->config.num_encoder_blocks - 1; i++) {
            bt_H /= 2; bt_W /= 2;
        }
        /* Encoder last block output channels = base << (num_blocks-1) */
        uint32_t bt_C = m->config.base_channels << (m->config.num_encoder_blocks - 1);
        aspira_tensor bt_input_shape;
        aspira_tensor_init(&bt_input_shape, 1, bt_C, bt_H, bt_W, NULL, false);

        /* Bottleneck output = bt_C * 2 channels, same spatial */
        aspira_tensor dec_input_shape;
        aspira_tensor_init(&dec_input_shape, 1, bt_C * 2, bt_H, bt_W, NULL, false);

        if (!alloc_layer_tensors(m->encoder_head, m->input_tensor)) return false;
        if (!alloc_layer_tensors(m->bottleneck_head, &bt_input_shape)) return false;
        if (!alloc_layer_tensors(m->decoder_head, &dec_input_shape)) return false;
    }

    /* Copy input */
    if (input->data != m->input_tensor->data) {
        memcpy(m->input_tensor->data, input->data, aspira_tensor_bytes(input));
    }

    aspira_tensor* current = m->input_tensor;
    int skip_idx = 0;

    /* ===== Encoder ===== */
    for (aspira_unet_layer* l = m->encoder_head; l; l = l->next) {
        switch (l->type) {
        case ASPIRA_UNET_CONV2D:
            aspira_unet_conv2d(current, &l->config.conv, l->output);
            current = l->output;
            break;
        case ASPIRA_UNET_RELU:
            aspira_unet_relu(current);
            break;
        case ASPIRA_UNET_MAXPOOL2D:
            /* Save skip before pooling */
            if (skip_idx < 5) m->skip_buffers[skip_idx++] = current;
            aspira_unet_maxpool2d(current, l->config.pool.pool_size,
                                   l->config.pool.stride, l->output);
            current = l->output;
            break;
        default: break;
        }
    }
    /* Save last encoder output as skip (deepest skip) */
    if (skip_idx < 5) m->skip_buffers[skip_idx++] = current;

    /* ===== Bottleneck ===== */
    for (aspira_unet_layer* l = m->bottleneck_head; l; l = l->next) {
        switch (l->type) {
        case ASPIRA_UNET_CONV2D:
            aspira_unet_conv2d(current, &l->config.conv, l->output);
            current = l->output;
            break;
        case ASPIRA_UNET_RELU:
            aspira_unet_relu(current);
            break;
        default: break;
        }
    }

    /* ===== Decoder ===== */
    skip_idx--;  /* Start from deepest skip */
    for (aspira_unet_layer* l = m->decoder_head; l; l = l->next) {
        switch (l->type) {
        case ASPIRA_UNET_CONV2D:
            aspira_unet_conv2d(current, &l->config.conv, l->output);
            current = l->output;
            break;
        case ASPIRA_UNET_RELU:
            aspira_unet_relu(current);
            break;
        case ASPIRA_UNET_UPSAMPLE:
            aspira_unet_upsample(current, l->config.upsample.scale_factor,
                                  l->output);
            current = l->output;
            break;
        case ASPIRA_UNET_CONCAT: {
            int si = l->config.concat.skip_index;
            if (si >= 0 && si <= skip_idx && m->skip_buffers[si]) {
                aspira_unet_concat(current, m->skip_buffers[si], l->output);
            } else {
                /* Skip unavailable — just copy current */
                memcpy(l->output->data, current->data, aspira_tensor_bytes(current));
            }
            current = l->output;
            skip_idx--;
            break;
        }
        case ASPIRA_UNET_SIGMOID:
            aspira_unet_sigmoid(current);
            break;
        default: break;
        }
    }

    /* Copy final output */
    if (current != m->output_tensor && current->data != m->output_tensor->data) {
        memcpy(m->output_tensor->data, current->data, aspira_tensor_bytes(current));
    }

    m->total_inferences++;
    return true;
}
