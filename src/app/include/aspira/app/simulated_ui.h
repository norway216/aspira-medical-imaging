/**
 * @file simulated_ui.h
 * @brief Console-based simulated UI for pipeline monitoring
 */

#ifndef ASPIRA_APP_SIMULATED_UI_H
#define ASPIRA_APP_SIMULATED_UI_H

#include <aspira/core/core.h>

#include <chrono>
#include <string>

namespace aspira {

struct PipelineStats {
    double   acquisition_fps = 0.0;
    double   processing_fps = 0.0;
    double   display_fps = 0.0;
    double   avg_frame_latency_us = 0.0;
    double   min_frame_latency_us = 0.0;
    double   max_frame_latency_us = 0.0;
    uint64_t frames_acquired = 0;
    uint64_t frames_processed = 0;
    uint64_t frames_displayed = 0;
    uint64_t frames_dropped = 0;
    uint64_t pool_allocations = 0;
    uint64_t pool_free = 0;
    uint64_t ring_buffer_usage = 0;
    uint64_t uptime_ms = 0;
    bool     pipeline_healthy = true;
};

class SimulatedUI {
public:
    SimulatedUI();
    ~SimulatedUI() = default;

    /**
     * @brief Update and refresh the display
     */
    void update(const PipelineStats& stats);

    /**
     * @brief Check for keyboard input (non-blocking)
     */
    bool has_input();

    /**
     * @brief Get last keypress
     */
    char get_input();

    /**
     * @brief Set display mode: 0=stats, 1=compact, 2=verbose
     */
    void set_display_mode(int mode) { display_mode_ = mode; }

    /**
     * @brief Show help screen
     */
    void show_help() const;

    /**
     * @brief Show startup banner
     */
    void show_banner() const;

    /**
     * @brief Print a simple ASCII representation of frame data
     */
    void print_frame_ascii(const aspira_frame* frame, int max_width = 80) const;

private:
    int display_mode_ = 0;
    std::chrono::steady_clock::time_point start_time_;
    bool ansi_supported_ = true;

    void clear_screen() const;
    void set_cursor(int row, int col) const;
    void print_stats_screen(const PipelineStats& stats) const;
    void print_compact_stats(const PipelineStats& stats) const;

    static const char* health_icon(bool healthy);
};

} // namespace aspira

#endif /* ASPIRA_APP_SIMULATED_UI_H */
