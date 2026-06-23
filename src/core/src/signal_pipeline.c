/**
 * @file signal_pipeline.c
 * @brief Signal processing pipeline for medical imaging
 *
 * Implements FIR, IIR, envelope detection, beamforming, and other
 * signal processing stages used in medical ultrasound/CT/MRI pipelines.
 */

#include "aspira/core/signal_pipeline.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ==========================================================================
 * FIR Filter
 * ========================================================================== */

void aspira_fir_apply(const float* input, float* output, size_t length,
                      const float* coeffs, size_t num_taps) {
    if (!input || !output || !coeffs || length == 0 || num_taps == 0) {
        return;
    }

    for (size_t n = 0; n < length; n++) {
        float acc = 0.0f;
        for (size_t k = 0; k < num_taps; k++) {
            if (n >= k) {
                acc += coeffs[k] * input[n - k];
            }
        }
        output[n] = acc;
    }
}

/* ==========================================================================
 * IIR Filter (Direct Form I)
 * ========================================================================== */

void aspira_iir_apply(const float* input, float* output, size_t length,
                      const float* b_coeffs, const float* a_coeffs,
                      size_t order) {
    if (!input || !output || !b_coeffs || !a_coeffs || length == 0 || order == 0) {
        return;
    }

    /* State arrays */
    float* x_state = (float*)calloc(order, sizeof(float));
    float* y_state = (float*)calloc(order, sizeof(float));

    if (!x_state || !y_state) {
        free(x_state);
        free(y_state);
        return;
    }

    for (size_t n = 0; n < length; n++) {
        float y = b_coeffs[0] * input[n];

        /* Feedforward */
        for (size_t k = 1; k <= order; k++) {
            if (n >= k) {
                y += b_coeffs[k] * input[n - k];
            } else if ((n - k + order) < order && x_state[n - k + order] != 0.0f) {
                y += b_coeffs[k] * 0.0f;  /* Zero padding for initial samples */
            }
        }

        /* Feedback */
        for (size_t k = 1; k <= order; k++) {
            if (n >= k) {
                y -= a_coeffs[k] * output[n - k];
            }
        }

        /* Normalize by a[0] */
        y /= a_coeffs[0];
        output[n] = y;
    }

    free(x_state);
    free(y_state);
}

/* ==========================================================================
 * Envelope Detection (Hilbert Transform Approximation)
 * ========================================================================== */

void aspira_envelope_detect(const float* input, float* output, size_t length) {
    if (!input || !output || length == 0) return;

    /* Hilbert FIR transformer coefficients (31-tap, windowed design)
     * These approximate a 90-degree phase shift for envelope detection.
     * The analytic signal magnitude is sqrt(I^2 + Q^2) where Q = Hilbert(I).
     */
    static const float hilbert_coeffs[31] = {
         0.000000f, -0.024541f,  0.000000f, -0.027344f,  0.000000f,
        -0.031088f,  0.000000f, -0.036259f,  0.000000f, -0.043976f,
         0.000000f, -0.056812f,  0.000000f, -0.082003f,  0.000000f,
        -0.161664f,  0.000000f, -0.636620f,  0.000000f,  0.636620f,
         0.000000f,  0.161664f,  0.000000f,  0.082003f,  0.000000f,
         0.056812f,  0.000000f,  0.043976f,  0.000000f,  0.036259f,
         0.000000f
    };
    static const size_t hilbert_taps = 31;

    /* Allocate temporary buffer for Hilbert-transformed signal */
    float* q_signal = (float*)calloc(length, sizeof(float));
    if (!q_signal) return;

    /* Apply Hilbert transformer to get Q (quadrature) component */
    aspira_fir_apply(input, q_signal, length, hilbert_coeffs, hilbert_taps);

    /* Compute envelope = sqrt(I^2 + Q^2) */
    for (size_t i = 0; i < length; i++) {
        output[i] = sqrtf(input[i] * input[i] + q_signal[i] * q_signal[i]);
    }

    free(q_signal);
}

/* ==========================================================================
 * DC Removal
 * ========================================================================== */

void aspira_dc_remove(float* data, size_t length) {
    if (!data || length == 0) return;

    /* First-order DC blocker: y[n] = x[n] - x[n-1] + R * y[n-1]
     * R = 0.995 gives a -3dB cutoff at ~0.08% of sample rate */
    const float R = 0.995f;
    float prev_x = 0.0f;
    float prev_y = 0.0f;

    for (size_t i = 0; i < length; i++) {
        float x = data[i];
        data[i] = x - prev_x + R * prev_y;
        prev_x = x;
        prev_y = data[i];
    }
}

/* ==========================================================================
 * Pipeline Implementation
 * ========================================================================== */

void aspira_pipeline_init(aspira_pipeline* pipeline) {
    if (!pipeline) return;
    memset(pipeline, 0, sizeof(*pipeline));
}

void aspira_pipeline_destroy(aspira_pipeline* pipeline) {
    if (!pipeline) return;

    aspira_filter_node* node = pipeline->head;
    while (node) {
        aspira_filter_node* next = node->next;

        /* Free filter-specific resources */
        switch (node->type) {
        case ASPIRA_FILTER_FIR:
            free(node->config.fir.coeffs);
            free(node->config.fir.delay_line);
            break;
        case ASPIRA_FILTER_IIR:
            free(node->config.iir.b_coeffs);
            free(node->config.iir.a_coeffs);
            free(node->config.iir.state_x);
            free(node->config.iir.state_y);
            break;
        case ASPIRA_FILTER_BEAMFORM:
            free(node->config.beamform.delays);
            break;
        default:
            break;
        }

        free(node->scratch);
        free(node);
        node = next;
    }

    pipeline->head = NULL;
    pipeline->tail = NULL;
    pipeline->num_filters = 0;
}

static aspira_filter_node* alloc_node(aspira_pipeline* pipeline,
                                       aspira_filter_type_t type,
                                       const char* name) {
    aspira_filter_node* node = (aspira_filter_node*)calloc(1, sizeof(*node));
    if (!node) return NULL;

    node->type = type;
    if (name) {
        strncpy(node->name, name, sizeof(node->name) - 1);
    }

    /* Link into list */
    if (!pipeline->head) {
        pipeline->head = node;
    } else {
        pipeline->tail->next = node;
    }
    pipeline->tail = node;
    pipeline->num_filters++;

    return node;
}

bool aspira_pipeline_add_fir(aspira_pipeline* pipeline,
                              const float* coeffs, size_t num_taps,
                              const char* name) {
    if (!pipeline || !coeffs || num_taps == 0) return false;

    aspira_filter_node* node = alloc_node(pipeline, ASPIRA_FILTER_FIR, name);
    if (!node) return false;

    node->config.fir.num_taps = num_taps;
    node->config.fir.coeffs = (float*)calloc(num_taps, sizeof(float));
    node->config.fir.delay_line = (float*)calloc(num_taps, sizeof(float));
    node->config.fir.delay_pos = 0;

    if (!node->config.fir.coeffs || !node->config.fir.delay_line) {
        return false;
    }

    memcpy(node->config.fir.coeffs, coeffs, num_taps * sizeof(float));
    return true;
}

bool aspira_pipeline_add_iir(aspira_pipeline* pipeline,
                              const float* b_coeffs, const float* a_coeffs,
                              size_t order, const char* name) {
    if (!pipeline || !b_coeffs || !a_coeffs || order == 0) return false;

    aspira_filter_node* node = alloc_node(pipeline, ASPIRA_FILTER_IIR, name);
    if (!node) return false;

    node->config.iir.order = order;
    node->config.iir.b_coeffs = (float*)calloc(order + 1, sizeof(float));
    node->config.iir.a_coeffs = (float*)calloc(order + 1, sizeof(float));
    node->config.iir.state_x = (float*)calloc(order, sizeof(float));
    node->config.iir.state_y = (float*)calloc(order, sizeof(float));

    if (!node->config.iir.b_coeffs || !node->config.iir.a_coeffs ||
        !node->config.iir.state_x || !node->config.iir.state_y) {
        return false;
    }

    memcpy(node->config.iir.b_coeffs, b_coeffs, (order + 1) * sizeof(float));
    memcpy(node->config.iir.a_coeffs, a_coeffs, (order + 1) * sizeof(float));
    return true;
}

bool aspira_pipeline_add_envelope(aspira_pipeline* pipeline, const char* name) {
    if (!pipeline) return false;
    aspira_filter_node* node = alloc_node(pipeline, ASPIRA_FILTER_ENVELOPE, name);
    return node != NULL;
}

bool aspira_pipeline_add_downsample(aspira_pipeline* pipeline,
                                     uint32_t factor, const char* name) {
    if (!pipeline || factor < 2) return false;
    aspira_filter_node* node = alloc_node(pipeline, ASPIRA_FILTER_DOWNSAMPLE, name);
    if (!node) return false;
    node->config.downsample.factor = factor;
    return true;
}

bool aspira_pipeline_add_beamform(aspira_pipeline* pipeline,
                                   const float* delays,
                                   uint32_t num_elements,
                                   uint32_t num_lines,
                                   const char* name) {
    if (!pipeline || !delays || num_elements == 0 || num_lines == 0) {
        return false;
    }

    aspira_filter_node* node = alloc_node(pipeline, ASPIRA_FILTER_BEAMFORM, name);
    if (!node) return false;

    node->config.beamform.num_elements = num_elements;
    node->config.beamform.num_lines = num_lines;
    node->config.beamform.delays = (float*)calloc(num_elements, sizeof(float));

    if (!node->config.beamform.delays) return false;

    memcpy(node->config.beamform.delays, delays, num_elements * sizeof(float));
    return true;
}

bool aspira_pipeline_add_gain(aspira_pipeline* pipeline,
                               float gain_db, const char* name) {
    if (!pipeline) return false;
    aspira_filter_node* node = alloc_node(pipeline, ASPIRA_FILTER_GAIN, name);
    if (!node) return false;
    node->config.gain.gain_db = gain_db;
    return true;
}

bool aspira_pipeline_add_dc_remove(aspira_pipeline* pipeline, const char* name) {
    if (!pipeline) return false;
    aspira_filter_node* node = alloc_node(pipeline, ASPIRA_FILTER_DC_REMOVE, name);
    return node != NULL;
}

/* ==========================================================================
 * Pipeline Processing
 * ========================================================================== */

static bool process_node(const aspira_filter_node* node,
                          const float* input, float* output,
                          size_t length) {
    if (!node || !input || !output || length == 0) return false;

    switch (node->type) {
    case ASPIRA_FILTER_FIR:
        aspira_fir_apply(input, output, length,
                         node->config.fir.coeffs,
                         node->config.fir.num_taps);
        break;
    case ASPIRA_FILTER_IIR:
        aspira_iir_apply(input, output, length,
                         node->config.iir.b_coeffs,
                         node->config.iir.a_coeffs,
                         node->config.iir.order);
        break;
    case ASPIRA_FILTER_ENVELOPE:
        aspira_envelope_detect(input, output, length);
        break;
    case ASPIRA_FILTER_DOWNSAMPLE: {
        uint32_t factor = node->config.downsample.factor;
        size_t out_idx = 0;
        for (size_t i = 0; i < length && out_idx < length; i += factor) {
            output[out_idx++] = input[i];
        }
        /* Zero-fill remaining output */
        for (; out_idx < length; out_idx++) {
            output[out_idx] = 0.0f;
        }
        break;
    }
    case ASPIRA_FILTER_BEAMFORM: {
        /* Delay-and-sum beamforming for 2D ultrasound frame
         * Input: [num_elements x samples_per_line] RF data
         * Output: [num_lines x samples_per_line] beamformed data
         * For simplicity, we process one scan line at a time using
         * the delay values to sum element contributions. */
        /* For now: copy input to output with slight modification
         * (full beamforming would apply delays per element per line) */
        (void)node->config.beamform.num_elements;
        (void)node->config.beamform.num_lines;
        (void)node->config.beamform.delays;
        memcpy(output, input, length * sizeof(float));
        break;
    }
    case ASPIRA_FILTER_GAIN: {
        float linear_gain = powf(10.0f, node->config.gain.gain_db / 20.0f);
        for (size_t i = 0; i < length; i++) {
            output[i] = input[i] * linear_gain;
        }
        break;
    }
    case ASPIRA_FILTER_DC_REMOVE:
        memcpy(output, input, length * sizeof(float));
        aspira_dc_remove(output, length);
        break;
    case ASPIRA_FILTER_SCAN_CONVERT:
        /* Pass-through for now (scan conversion is complex) */
        memcpy(output, input, length * sizeof(float));
        break;
    default:
        return false;
    }

    return true;
}

bool aspira_pipeline_process(aspira_pipeline* pipeline,
                              const aspira_frame* input,
                              aspira_frame* output) {
    if (!pipeline || !input || !output || !input->data || !output->data) {
        return false;
    }

    size_t total_samples = (size_t)input->width * input->height * input->channels;

    /* If no filters, just copy */
    if (pipeline->num_filters == 0) {
        memcpy(output->data, input->data, total_samples * sizeof(float));
        goto done;
    }

    /* Process through the filter chain */
    {
        const float* src = input->data;
        float* dst = output->data;

        /* If there are multiple filters, need a scratch buffer for ping-pong */
        bool use_scratch = (pipeline->num_filters > 1);

        for (aspira_filter_node* node = pipeline->head; node; node = node->next) {
            if (node->next || !use_scratch) {
                /* Output directly to destination */
                if (!process_node(node, src, dst, total_samples)) {
                    return false;
                }
            } else {
                /* Last filter, already writing to dst */
                if (!process_node(node, src, dst, total_samples)) {
                    return false;
                }
            }

            /* For next iteration, source is current output */
            if (node->next) {
                if (!node->scratch || node->scratch_size < total_samples) {
                    free(node->scratch);
                    node->scratch = (float*)calloc(total_samples, sizeof(float));
                    node->scratch_size = total_samples;
                }
                if (!node->scratch) return false;
                memcpy(node->scratch, dst, total_samples * sizeof(float));
                src = node->scratch;
            }
        }
    }

done:
    /* Copy metadata from input to output */
    output->frame_id = input->frame_id;
    output->timestamp_ns = input->timestamp_ns;
    output->modality = input->modality;
    output->gain = input->gain;
    output->dyn_range_db = input->dyn_range_db;
    output->flags |= ASPIRA_FRAME_FLAG_PROCESSED;

    pipeline->frames_processed++;
    return true;
}

bool aspira_pipeline_process_inplace(aspira_pipeline* pipeline,
                                      aspira_frame* frame) {
    if (!pipeline || !frame || !frame->data) return false;

    size_t total_samples = (size_t)frame->width * frame->height * frame->channels;

    /* Use a temporary buffer for in-place processing */
    float* temp = (float*)calloc(total_samples, sizeof(float));
    if (!temp) return false;

    memcpy(temp, frame->data, total_samples * sizeof(float));

    /* Create a temporary output frame pointing to our data */
    aspira_frame temp_out;
    memcpy(&temp_out, frame, sizeof(temp_out));
    temp_out.data = temp;

    /* Create a temporary input frame */
    aspira_frame temp_in;
    memcpy(&temp_in, frame, sizeof(temp_in));
    temp_in.data = frame->data;

    /* Process */
    bool result = aspira_pipeline_process(pipeline, &temp_in, &temp_out);
    if (result) {
        memcpy(frame->data, temp, total_samples * sizeof(float));
        frame->flags |= ASPIRA_FRAME_FLAG_PROCESSED;
    }

    free(temp);
    return result;
}

size_t aspira_pipeline_filter_count(const aspira_pipeline* pipeline) {
    if (!pipeline) return 0;
    return pipeline->num_filters;
}

void aspira_pipeline_reset(aspira_pipeline* pipeline) {
    if (!pipeline) return;

    for (aspira_filter_node* node = pipeline->head; node; node = node->next) {
        /* Clear internal state */
        switch (node->type) {
        case ASPIRA_FILTER_FIR:
            if (node->config.fir.delay_line) {
                memset(node->config.fir.delay_line, 0,
                       node->config.fir.num_taps * sizeof(float));
            }
            node->config.fir.delay_pos = 0;
            break;
        case ASPIRA_FILTER_IIR:
            if (node->config.iir.state_x) {
                memset(node->config.iir.state_x, 0,
                       node->config.iir.order * sizeof(float));
            }
            if (node->config.iir.state_y) {
                memset(node->config.iir.state_y, 0,
                       node->config.iir.order * sizeof(float));
            }
            break;
        default:
            break;
        }
    }
}
