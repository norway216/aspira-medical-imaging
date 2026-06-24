/**
 * @file simulated_ui.cpp
 * @brief Console-based UI implementation
 */

#include "aspira/app/simulated_ui.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <termios.h>
#include <unistd.h>

namespace aspira {

SimulatedUI::SimulatedUI()
    : start_time_(std::chrono::steady_clock::now()) {
    /* Check for ANSI support */
    const char* term = getenv("TERM");
    ansi_supported_ = term != nullptr;
}

void SimulatedUI::clear_screen() const {
    if (ansi_supported_) {
        std::cout << "\033[2J\033[H";
    }
}

void SimulatedUI::set_cursor(int row, int col) const {
    if (ansi_supported_) {
        std::cout << "\033[" << row << ";" << col << "H";
    }
}

const char* SimulatedUI::health_icon(bool healthy) {
    return healthy ? "\033[32m● HEALTHY\033[0m" : "\033[31m● FAULT\033[0m";
}

void SimulatedUI::show_banner() const {
    clear_screen();
    std::cout << "\033[1;36m";
    std::cout << "╔══════════════════════════════════════════════════╗\n";
    std::cout << "║         ASPIRA MEDICAL IMAGING FRAMEWORK         ║\n";
    std::cout << "║              High-Performance Pipeline           ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n";
    std::cout << "\033[0m\n";
    std::cout << "  Version: 0.1.0\n";
    std::cout << "  Build:   " << __DATE__ << " " << __TIME__ << "\n";
    std::cout << "  Target:  Ultrasound / CT / MRI Pipeline\n";
    std::cout << "\n  Press 'h' for help, 'q' to quit\n";
    std::cout << "\n──────────────────────────────────────────────────\n\n";
}

void SimulatedUI::show_help() const {
    std::cout << "\n\033[1mKeyboard Controls:\033[0m\n";
    std::cout << "  q     - Quit\n";
    std::cout << "  h     - This help\n";
    std::cout << "  0     - Stats display mode\n";
    std::cout << "  1     - Compact display mode\n";
    std::cout << "  2     - Verbose display mode\n";
    std::cout << "  s     - Snapshot current stats\n";
    std::cout << "  p     - Pause/Resume display\n";
    std::cout << std::endl;
}

void SimulatedUI::print_compact_stats(const PipelineStats& stats) const {
    std::cout << "\r";  /* Carriage return for in-place update */
    std::cout << health_icon(stats.pipeline_healthy) << " | ";
    std::cout << "FPS: A=" << std::fixed << std::setprecision(1) << stats.acquisition_fps;
    std::cout << " P=" << stats.processing_fps;
    std::cout << " D=" << stats.display_fps;
    std::cout << " | Latency: " << (int)stats.avg_frame_latency_us << "us";
    std::cout << " | Dropped: " << stats.frames_dropped;
    std::cout << " | Pool: " << stats.pool_allocations - stats.pool_free;
    std::cout << " | Uptime: " << stats.uptime_ms / 1000 << "s";
    std::cout << "  " << std::flush;
}

void SimulatedUI::print_stats_screen(const PipelineStats& stats) const {
    clear_screen();
    std::cout << "\033[1;36m═══ ASPIRA Pipeline Monitor ═══\033[0m";
    std::cout << "          Uptime: " << stats.uptime_ms / 1000 << "s\n\n";

    /* Health */
    std::cout << "  Pipeline Status: " << health_icon(stats.pipeline_healthy) << "\n\n";

    /* FPS */
    std::cout << "\033[1m  Frame Rate:\033[0m\n";
    std::cout << "    Acquisition : " << std::fixed << std::setprecision(1)
              << stats.acquisition_fps << " fps\n";
    std::cout << "    Processing  : " << stats.processing_fps << " fps\n";
    std::cout << "    Display     : " << stats.display_fps << " fps\n\n";

    /* Latency */
    std::cout << "\033[1m  Frame Latency:\033[0m\n";
    std::cout << "    Average: " << (int)stats.avg_frame_latency_us << " us\n";
    std::cout << "    Min:     " << (int)stats.min_frame_latency_us << " us\n";
    std::cout << "    Max:     " << (int)stats.max_frame_latency_us << " us\n\n";

    /* Counters */
    std::cout << "\033[1m  Frame Counters:\033[0m\n";
    std::cout << "    Acquired:  " << stats.frames_acquired << "\n";
    std::cout << "    Processed: " << stats.frames_processed << "\n";
    std::cout << "    Displayed: " << stats.frames_displayed << "\n";
    std::cout << "    Dropped:   " << stats.frames_dropped << "\n\n";

    /* Pool */
    std::cout << "\033[1m  Memory Pool:\033[0m\n";
    std::cout << "    Allocated: " << stats.pool_allocations << "\n";
    std::cout << "    Freed:     " << stats.pool_free << "\n";
    std::cout << "    In Use:    " << (stats.pool_allocations - stats.pool_free) << "\n";
    std::cout << "    RB Usage:  " << stats.ring_buffer_usage << "%\n\n";

    std::cout << "──────────────────────────────────────────\n";
    std::cout << "  Press 'q' to quit, 'h' for help\n";
}

void SimulatedUI::update(const PipelineStats& stats) {
    switch (display_mode_) {
    case 0:
        print_stats_screen(stats);
        break;
    case 1:
        print_compact_stats(stats);
        break;
    case 2:
        /* Verbose: show full stats */
        print_stats_screen(stats);
        break;
    default:
        break;
    }
}

bool SimulatedUI::has_input() {
    struct timeval tv = {0, 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

char SimulatedUI::get_input() {
    char c = '\0';
    if (read(STDIN_FILENO, &c, 1) > 0) {
        return c;
    }
    return '\0';
}

void SimulatedUI::print_frame_ascii(const aspira_frame* frame,
                                     int max_width) const {
    if (!frame || !frame->data) return;

    uint32_t w = frame->width;
    uint32_t h = frame->height;

    /* Downsample for display */
    int dw = (w > (uint32_t)max_width) ? max_width : (int)w;
    int dh = (dw * (int)h / (int)w) / 2;
    if (dh > 30) dh = 30;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;

    /* Find data range for auto-normalization */
    size_t total = (size_t)w * h;
    float fmin = frame->data[0], fmax = frame->data[0];
    for (size_t i = 1; i < total; i++) {
        float v = frame->data[i];
        if (v < fmin) fmin = v;
        if (v > fmax) fmax = v;
    }
    float range = fmax - fmin;
    if (range < 1e-10f) range = 1.0f;

    /* Unicode block characters for 2x vertical resolution:
       ' ' = both dark, '▀' = top bright, '▄' = bottom bright, '█' = both bright */
    const char* blocks[] = {" ", "▄", "▀", "█"};

    std::cout << "\n  ┌─ Frame #" << frame->frame_id
              << " (" << w << "x" << h << ") "
              << "range: [" << fmin << ", " << fmax << "] ─┐\n";

    for (int y = 0; y < dh; y++) {
        std::cout << "  │";
        for (int x = 0; x < dw; x++) {
            int sx = x * (int)w / dw;
            int sy_top = y * 2 * (int)h / (dh * 2);
            int sy_bot = (y * 2 + 1) * (int)h / (dh * 2);

            if (sy_bot >= (int)h) sy_bot = (int)h - 1;

            float v_top = (frame->data[sy_top * w + sx] - fmin) / range;
            float v_bot = (frame->data[sy_bot * w + sx] - fmin) / range;

            int top_bright = (v_top > 0.45f) ? 1 : 0;
            int bot_bright = (v_bot > 0.45f) ? 1 : 0;
            int idx = top_bright * 2 + bot_bright;

            std::cout << blocks[idx];
        }
        std::cout << "│\n";
    }
    std::cout << "  └";
    for (int x = 0; x < dw; x++) std::cout << "─";
    std::cout << "┘\n";
    std::cout << "  Bright: █  Dark:    (Unicode blocks, double resolution)\n";
    std::cout << "  Press 'e' to export as PGM, 'm' for PPM\n";
}

} // namespace aspira
