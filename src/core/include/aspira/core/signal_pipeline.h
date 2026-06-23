/**
 * @file signal_pipeline.h
 * @brief Signal processing pipeline for medical imaging
 *
 * Implements a chain of filters for ultrasound/CT/MRI signal processing:
 *   - FIR filter (convolution-based)
 *   - IIR filter (Direct Form I)
 *   - Envelope detection (Hilbert transform approximation)
 *   - Downsampling / decimation
 *   - Simulated beamforming (delay-and-sum)
 */

#ifndef ASPIRA_SIGNAL_PIPELINE_H
#define ASPIRA_SIGNAL_PIPELINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Filter Types
 * ========================================================================== */

typedef enum {
    ASPIRA_FILTER_FIR = 0,
    ASPIRA_FILTER_IIR,
    ASPIRA_FILTER_ENVELOPE,
    ASPIRA_FILTER_DOWNSAMPLE,
    ASPIRA_FILTER_BEAMFORM,
    ASPIRA_FILTER_SCAN_CONVERT,
    ASPIRA_FILTER_GAIN,
    ASPIRA_FILTER_DC_REMOVE,
} aspira_filter_type_t;

/* ==========================================================================
 * Probe Parameters (simulated)
 * ========================================================================== */

typedef struct {
    uint32_t num_elements;          /* Probe element count (e.g., 64, 128) */
    float    center_frequency_hz;   /* Center frequency (e.g., 5.0e6) */
    float    sampling_rate_hz;      /* Sampling rate (e.g., 40.0e6) */
    float    sound_speed_mps;       /* Speed of sound (1540 m/s typical) */
    float    pitch_mm;              /* Element pitch */
    float    focal_depth_mm;        /* Focal depth */
} aspira_probe_params;

typedef struct {
    uint32_t num_lines;             /* Number of beamformed lines */
    float    sector_width_deg;      /* Sector width for phased array */
    float    start_depth_mm;        /* Start of imaging depth */
    float    end_depth_mm;          /* End of imaging depth */
} aspira_scan_params;

/* ==========================================================================
 * Filter Structures
 * ========================================================================== */

typedef struct {
    float*   coeffs;
    size_t   num_taps;
    float*   delay_line;           /* Internal delay line buffer */
    size_t   delay_pos;            /* Current position in delay line */
} aspira_fir_filter;

typedef struct {
    float*   b_coeffs;             /* Feedforward coefficients (numerator) */
    float*   a_coeffs;             /* Feedback coefficients (denominator) */
    size_t   order;
    float*   state_x;              /* Input history */
    float*   state_y;              /* Output history */
} aspira_iir_filter;

/* ==========================================================================
 * Filter Node (linked list of pipeline stages)
 * ========================================================================== */

struct aspira_filter_node;

typedef struct aspira_filter_node {
    aspira_filter_type_t  type;
    char                  name[32];

    union {
        aspira_fir_filter   fir;
        aspira_iir_filter   iir;
        struct {
            uint32_t factor;           /* Downsampling factor */
        } downsample;
        struct {
            float*   delays;           /* Per-element delays (s) */
            uint32_t num_elements;
            uint32_t num_lines;
        } beamform;
        struct {
            uint32_t src_width;
            uint32_t src_height;
            uint32_t dst_width;
            uint32_t dst_height;
        } scan_convert;
        struct {
            float gain_db;
        } gain;
    } config;

    /* Scratch buffers for intermediate results */
    float* scratch;
    size_t scratch_size;

    struct aspira_filter_node* next;
} aspira_filter_node;

/* ==========================================================================
 * Signal Pipeline
 * ========================================================================== */

typedef struct {
    aspira_filter_node* head;
    aspira_filter_node* tail;
    size_t              num_filters;

    /* Processing statistics */
    uint64_t frames_processed;
    uint64_t total_processing_time_ns;
} aspira_pipeline;

/**
 * @brief Initialize an empty pipeline
 */
void aspira_pipeline_init(aspira_pipeline* pipeline);

/**
 * @brief Destroy pipeline and free all filters
 */
void aspira_pipeline_destroy(aspira_pipeline* pipeline);

/**
 * @brief Add an FIR filter to the pipeline
 * @param coeffs Filter coefficients (will be copied)
 * @param num_taps Number of filter taps
 * @param name Human-readable name for the filter
 * @return true on success
 */
bool aspira_pipeline_add_fir(aspira_pipeline* pipeline,
                              const float* coeffs, size_t num_taps,
                              const char* name);

/**
 * @brief Add an IIR filter to the pipeline
 * @param b_coeffs Feedforward coefficients (will be copied)
 * @param a_coeffs Feedback coefficients (a[0] must be 1.0)
 * @param order Filter order
 * @param name Human-readable name
 * @return true on success
 */
bool aspira_pipeline_add_iir(aspira_pipeline* pipeline,
                              const float* b_coeffs, const float* a_coeffs,
                              size_t order, const char* name);

/**
 * @brief Add an envelope detector (Hilbert-based)
 * Uses a FIR Hilbert transformer to compute analytic signal magnitude
 * @return true on success
 */
bool aspira_pipeline_add_envelope(aspira_pipeline* pipeline, const char* name);

/**
 * @brief Add a downsampling stage
 * @param factor Downsampling factor (e.g., 2 = halve the sample rate)
 * @return true on success
 */
bool aspira_pipeline_add_downsample(aspira_pipeline* pipeline,
                                     uint32_t factor, const char* name);

/**
 * @brief Add a simulated beamforming stage (delay-and-sum)
 * @param delays Per-element delay values in seconds (array of num_elements)
 * @param num_elements Number of probe elements
 * @param num_lines Number of output scan lines
 * @return true on success
 */
bool aspira_pipeline_add_beamform(aspira_pipeline* pipeline,
                                   const float* delays,
                                   uint32_t num_elements,
                                   uint32_t num_lines,
                                   const char* name);

/**
 * @brief Add a gain stage
 * @param gain_db Gain in dB
 * @return true on success
 */
bool aspira_pipeline_add_gain(aspira_pipeline* pipeline,
                               float gain_db, const char* name);

/**
 * @brief Add a DC removal (high-pass) filter
 * Simple first-order DC blocker: y[n] = x[n] - x[n-1] + 0.995*y[n-1]
 */
bool aspira_pipeline_add_dc_remove(aspira_pipeline* pipeline, const char* name);

/**
 * @brief Process a frame through the entire pipeline
 * @param pipeline The pipeline
 * @param input Input frame
 * @param output Output frame (must be pre-allocated with sufficient data)
 * @return true on success
 */
bool aspira_pipeline_process(aspira_pipeline* pipeline,
                              const aspira_frame* input,
                              aspira_frame* output);

/**
 * @brief Process a frame in-place (modifies input data)
 * @return true on success
 */
bool aspira_pipeline_process_inplace(aspira_pipeline* pipeline,
                                      aspira_frame* frame);

/**
 * @brief Get number of filters in the pipeline
 */
size_t aspira_pipeline_filter_count(const aspira_pipeline* pipeline);

/**
 * @brief Reset all filter states (e.g., for new scan session)
 */
void aspira_pipeline_reset(aspira_pipeline* pipeline);

/* ==========================================================================
 * Standalone Filter Functions (for direct use without pipeline)
 * ========================================================================== */

/**
 * @brief Apply FIR filter to a 1D signal
 * @param input Input signal
 * @param output Output signal (can be same as input for in-place)
 * @param length Signal length
 * @param coeffs Filter coefficients
 * @param num_taps Number of taps
 */
void aspira_fir_apply(const float* input, float* output, size_t length,
                      const float* coeffs, size_t num_taps);

/**
 * @brief Apply IIR filter (Direct Form I) to a 1D signal
 * @param input Input signal
 * @param output Output signal
 * @param length Signal length
 * @param b_coeffs Feedforward coefficients
 * @param a_coeffs Feedback coefficients (a[0] = 1.0)
 * @param order Filter order
 */
void aspira_iir_apply(const float* input, float* output, size_t length,
                      const float* b_coeffs, const float* a_coeffs,
                      size_t order);

/**
 * @brief Compute envelope of a signal via Hilbert transform
 * @param input Real-valued signal
 * @param output Envelope (magnitude of analytic signal)
 * @param length Signal length
 */
void aspira_envelope_detect(const float* input, float* output, size_t length);

/**
 * @brief Remove DC offset from a signal
 * @param data Signal to process (in-place)
 * @param length Signal length
 */
void aspira_dc_remove(float* data, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* ASPIRA_SIGNAL_PIPELINE_H */
