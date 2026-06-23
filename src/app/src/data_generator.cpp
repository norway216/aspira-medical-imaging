/**
 * @file data_generator.cpp
 * @brief Simulated medical imaging data generator
 */

#include "aspira/app/data_generator.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace aspira {

DataGenerator::DataGenerator(const SimulatedProbeConfig& config)
    : config_(config)
    , rng_(42)  /* Fixed seed for reproducibility */
    , noise_dist_(0.0f, config.noise_level) {
}

void DataGenerator::add_target(const SimulatedTarget& target) {
    targets_.push_back(target);
}

void DataGenerator::clear_targets() {
    targets_.clear();
}

void DataGenerator::update_targets(float time_delta_s) {
    for (auto& t : targets_) {
        if (t.velocity_mm_s <= 0.0f) continue;

        float rad = t.direction_deg * M_PI / 180.0f;
        /* Axial movement (depth) */
        t.depth_mm += t.velocity_mm_s * cosf(rad) * time_delta_s;
        /* Lateral movement */
        t.lateral_mm += t.velocity_mm_s * sinf(rad) * time_delta_s;

        /* Keep targets within imaging field */
        if (t.depth_mm < 1.0f) t.depth_mm = 1.0f;
        if (t.depth_mm > 80.0f) t.depth_mm = 80.0f;
        if (t.lateral_mm < -30.0f) t.lateral_mm = -30.0f;
        if (t.lateral_mm > 30.0f) t.lateral_mm = 30.0f;
    }
}

void DataGenerator::generate_scan_line(float* line_buffer, uint32_t line_idx,
                                        const std::vector<SimulatedTarget>& targets,
                                        float sim_time_s) {
    (void)sim_time_s;

    float lateral_pos = (float)line_idx * config_.pitch_mm;

    /* Wavelength in mm */
    float wavelength_mm = config_.sound_speed_mps / config_.frequency_hz;

    for (uint32_t s = 0; s < config_.samples_per_line; s++) {
        /* Sample depth in mm */
        float dt = 1.0f / config_.sampling_rate_hz;
        float depth_mm = (float)s * dt * config_.sound_speed_mps / 2.0f;
        float sample = 0.0f;

        /* Sum contributions from all targets */
        for (const auto& t : targets) {
            float dx = lateral_pos - t.lateral_mm;
            float dz = depth_mm - t.depth_mm;
            float dist = sqrtf(dx * dx + dz * dz);

            /* Gaussian envelope centered on target */
            float sigma = t.size_mm;
            float envelope = t.amplitude *
                expf(-((dz * dz) / (2.0f * sigma * sigma)));

            /* Phase for RF oscillation */
            float phase = 2.0f * M_PI * dist / wavelength_mm;
            sample += envelope * cosf(phase);
        }

        line_buffer[s] = sample;
    }
}

void DataGenerator::add_noise(float* data, size_t count) {
    for (size_t i = 0; i < count; i++) {
        data[i] += noise_dist_(rng_);
    }
}

aspira_frame* DataGenerator::generate_frame(uint64_t timestamp_ns,
                                             aspira_frame_pool* pool) {
    size_t total_samples = (size_t)config_.num_lines * config_.samples_per_line;
    size_t data_size = total_samples * sizeof(float);

    aspira_frame* frame = nullptr;
    float* data = nullptr;

    if (pool) {
        frame = aspira_frame_pool_alloc_frame(pool);
        if (frame) {
            data = frame->data;
        }
    } else {
        /* Heap allocation fallback */
        frame = (aspira_frame*)calloc(1, sizeof(aspira_frame));
        data = (float*)calloc(total_samples, sizeof(float));
        if (frame && data) {
            aspira_frame_init(frame, config_.num_lines, config_.samples_per_line,
                              1, data, data_size);
            frame->_internal = (void*)1; /* Mark as heap-allocated */
        } else {
            free(frame);
            free(data);
            return nullptr;
        }
    }

    /* Generate scan lines */
    float time_s = (float)frame_counter_ / config_.frame_rate;
    for (uint32_t line = 0; line < config_.num_lines; line++) {
        float* line_buf = data + (size_t)line * config_.samples_per_line;
        generate_scan_line(line_buf, line, targets_, time_s);
    }

    /* Add noise */
    add_noise(data, total_samples);

    /* Set frame metadata */
    frame->frame_id = frame_counter_++;
    frame->timestamp_ns = timestamp_ns;
    frame->flags = ASPIRA_FRAME_FLAG_VALID | ASPIRA_FRAME_FLAG_NEW;
    frame->modality = ASPIRA_MODALITY_ULTRASOUND;
    frame->gain = 0.0f;
    frame->dyn_range_db = 60.0f;

    return frame;
}

void DataGenerator::free_heap_frame(aspira_frame* frame) {
    if (!frame) return;
    /* If _internal is (void*)1, data was heap-allocated */
    if (frame->_internal == (void*)1) {
        free(frame->data);
        free(frame);
    }
}

} // namespace aspira
