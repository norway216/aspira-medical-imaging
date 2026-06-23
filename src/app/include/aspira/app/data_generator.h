/**
 * @file data_generator.h
 * @brief Simulated ultrasound/CT sensor data generator
 *
 * Generates synthetic RF echo data or CT sinogram data with configurable
 * targets, noise levels, and motion patterns for testing the pipeline.
 */

#ifndef ASPIRA_APP_DATA_GENERATOR_H
#define ASPIRA_APP_DATA_GENERATOR_H

#include <aspira/core/core.h>

#include <random>
#include <vector>

namespace aspira {

struct SimulatedTarget {
    float depth_mm;         /* Depth from probe surface */
    float lateral_mm;       /* Lateral position */
    float amplitude;        /* Reflection amplitude (0..1) */
    float size_mm;          /* Target size */
    float velocity_mm_s;    /* Movement velocity (0 = static) */
    float direction_deg;    /* Movement direction (0=axial, 90=lateral) */
};

struct SimulatedProbeConfig {
    uint32_t num_elements = 64;
    float    frequency_hz = 5.0e6f;       /* 5 MHz */
    float    sampling_rate_hz = 40.0e6f;  /* 40 MHz */
    float    sound_speed_mps = 1540.0f;
    float    pitch_mm = 0.3f;
    uint32_t samples_per_line = 2048;
    uint32_t num_lines = 128;
    float    frame_rate = 30.0f;
    float    noise_level = 0.05f;         /* 5% noise */
};

class DataGenerator {
public:
    explicit DataGenerator(const SimulatedProbeConfig& config);
    ~DataGenerator() = default;

    /**
     * @brief Generate one frame of simulated RF data
     * @param timestamp_ns Acquisition timestamp
     * @param pool Frame pool to allocate from (can be nullptr for heap alloc)
     * @return Allocated frame, or nullptr on failure
     */
    aspira_frame* generate_frame(uint64_t timestamp_ns,
                                  aspira_frame_pool* pool = nullptr);

    /**
     * @brief Add a synthetic target for simulation
     */
    void add_target(const SimulatedTarget& target);

    /**
     * @brief Remove all targets
     */
    void clear_targets();

    /**
     * @brief Update target positions (simulate motion over time)
     * @param time_delta_s Time step in seconds
     */
    void update_targets(float time_delta_s);

    /**
     * @brief Get current targets (read-only)
     */
    const std::vector<SimulatedTarget>& targets() const { return targets_; }

    /**
     * @brief Get probe configuration
     */
    const SimulatedProbeConfig& config() const { return config_; }

    /**
     * @brief Get frame counter
     */
    uint64_t frame_count() const { return frame_counter_; }

private:
    SimulatedProbeConfig config_;
    std::vector<SimulatedTarget> targets_;
    uint64_t frame_counter_ = 0;

    std::mt19937 rng_;
    std::normal_distribution<float> noise_dist_;

    /**
     * @brief Generate RF signal for one scan line
     */
    void generate_scan_line(float* line_buffer, uint32_t line_idx,
                            const std::vector<SimulatedTarget>& targets,
                            float sim_time_s);

    /**
     * @brief Add noise to signal
     */
    void add_noise(float* data, size_t count);

    /**
     * @brief Free a heap-allocated frame
     */
    void free_heap_frame(aspira_frame* frame);
};

} // namespace aspira

#endif /* ASPIRA_APP_DATA_GENERATOR_H */
