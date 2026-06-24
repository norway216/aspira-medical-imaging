/**
 * @file visual_test.cpp
 * @brief Standalone OpenCV visualization test to verify window rendering
 */

#include <aspira/core/core.h>
#include <aspira/app/visualizer.h>

#include <cmath>
#include <cstdio>
#include <thread>
#include <chrono>
#include <random>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main() {
    printf("=== OpenCV Visual Test ===\n");
    printf("Display: %s\n", getenv("DISPLAY") ? getenv("DISPLAY") : "NONE");

    /* Create a synthetic 256x256 frame with clear patterns */
    const uint32_t W = 256, H = 256;
    size_t total = (size_t)W * H;
    float* data = (float*)calloc(total, sizeof(float));
    if (!data) { printf("OOM\n"); return 1; }

    /* Test pattern: gradient + circle + cross */
    for (uint32_t y = 0; y < H; y++) {
        for (uint32_t x = 0; x < W; x++) {
            float dx = (float)x / (float)W - 0.5f;
            float dy = (float)y / (float)H - 0.5f;
            float dist = sqrtf(dx * dx + dy * dy);

            /* Background gradient (left-to-right) */
            float val = (float)x / (float)W;

            /* Bright circle in center */
            if (dist < 0.25f)
                val = 0.9f;

            /* Cross lines */
            if (fabsf(dx) < 0.01f || fabsf(dy) < 0.01f)
                val = 1.0f;

            /* Corner squares */
            if ((x < 30 && y < 30) || (x > W-30 && y > H-30))
                val = 0.3f;

            data[y * W + x] = val;
        }
    }

    aspira_frame frame;
    aspira_frame_init(&frame, W, H, 1, data, total * sizeof(float));

    /* Create OpenCV visualizer */
    aspira::Visualizer viz("Visual Test", 512, 512);
    printf("Window created. Running for 5 seconds...\n");

    int frame_id = 0;
    auto start = std::chrono::steady_clock::now();

    while (true) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - start).count();
        if (elapsed >= 5) break;

        /* Animate: shift the gradient phase */
        float phase = (float)elapsed * 0.5f;
        for (uint32_t y = 0; y < H; y++) {
            for (uint32_t x = 0; x < W; x++) {
                float dx = (float)x / (float)W - 0.5f;
                float dy = (float)y / (float)H - 0.5f;
                float dist = sqrtf(dx * dx + dy * dy);
                float val = (float)x / (float)W;
                if (dist < 0.25f) val = 0.9f;
                if (fabsf(dx) < 0.01f || fabsf(dy) < 0.01f) val = 1.0f;
                if ((x < 30 && y < 30) || (x > W-30 && y > H-30)) val = 0.3f;
                data[y * W + x] = val + 0.05f * sinf(phase + dist * 10.0f);
            }
        }
        frame.frame_id = frame_id++;

        char status[128];
        snprintf(status, sizeof(status),
                 "Frame: %d | Elapsed: %lds | Pattern: gradient+circle+cross",
                 frame_id, (long)elapsed);
        viz.set_status(status);
        viz.show(&frame, "Test Pattern");

        int key = viz.wait_key(30);
        if (key == 'q' || key == 'Q' || key == 27) {
            printf("User quit\n");
            break;
        }
    }

    printf("Test complete. Window should have shown patterns for 5 seconds.\n");
    free(data);
    return 0;
}
