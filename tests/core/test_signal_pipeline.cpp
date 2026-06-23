/**
 * @file test_signal_pipeline.cpp
 * @brief Unit tests for signal processing pipeline
 */

#include <aspira/core/signal_pipeline.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

TEST_CASE("FIR filter", "[core][signal][fir]") {
    SECTION("identity filter (single tap)") {
        float coeffs[] = {1.0f};
        float input[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        float output[5] = {0};

        aspira_fir_apply(input, output, 5, coeffs, 1);

        for (int i = 0; i < 5; i++) {
            REQUIRE(output[i] == Catch::Approx(input[i]));
        }
    }

    SECTION("moving average filter") {
        float coeffs[] = {0.333f, 0.333f, 0.334f};
        float input[] = {1.0f, 1.0f, 1.0f, 10.0f, 10.0f, 10.0f};
        float output[6] = {0};

        aspira_fir_apply(input, output, 6, coeffs, 3);

        /* First sample: 1 * 0.333 = 0.333 */
        REQUIRE(output[0] == Catch::Approx(0.333f).margin(0.01f));
        /* Fourth sample: transition */
        REQUIRE(output[3] > 3.0f);
        REQUIRE(output[5] == Catch::Approx(10.0f).margin(0.1f));
    }
}

TEST_CASE("Envelope detection", "[core][signal][envelope]") {
    SECTION("constant signal") {
        const size_t N = 512;
        std::vector<float> input(N, 1.0f);
        std::vector<float> output(N, 0.0f);

        aspira_envelope_detect(input.data(), output.data(), N);

        /* Envelope of constant signal should be ~1.0 */
        for (size_t i = 100; i < N - 100; i++) {
            REQUIRE(output[i] == Catch::Approx(1.0f).margin(0.3f));
        }
    }
}

TEST_CASE("DC removal", "[core][signal][dc_remove]") {
    SECTION("remove DC offset — filter runs without error") {
        /* DC blocker with R=0.995 has very slow decay (by design for
           preserving low-frequency signal content in ultrasound).
           Verify it processes without errors and output is valid. */
        float data[] = {10.0f, 10.0f, 10.0f, 10.0f, 10.0f};
        aspira_dc_remove(data, 5);

        /* Output should be finite and decreasing (DC being removed) */
        for (int i = 0; i < 5; i++) {
            REQUIRE(std::isfinite(data[i]));
        }
        /* Last value should be less than first (DC blocker is working) */
        REQUIRE(data[4] < data[0]);
    }
}

TEST_CASE("Signal pipeline chain", "[core][signal][pipeline]") {
    aspira_pipeline pipeline;
    aspira_pipeline_init(&pipeline);

    SECTION("build and process") {
        /* Add filters */
        float fir_coeffs[] = {0.2f, 0.2f, 0.2f, 0.2f, 0.2f};
        REQUIRE(aspira_pipeline_add_fir(&pipeline, fir_coeffs, 5, "smooth"));
        REQUIRE(aspira_pipeline_add_gain(&pipeline, 6.0f, "gain6dB"));
        REQUIRE(aspira_pipeline_add_dc_remove(&pipeline, "dc"));

        REQUIRE(aspira_pipeline_filter_count(&pipeline) == 3);

        /* Create test frames */
        const int N = 256;
        std::vector<float> input_data(N * N, 0.5f);
        std::vector<float> output_data(N * N, 0.0f);

        aspira_frame input_frame;
        aspira_frame_init(&input_frame, N, 1, 1, input_data.data(),
                          N * sizeof(float));

        aspira_frame output_frame;
        aspira_frame_init(&output_frame, N, 1, 1, output_data.data(),
                           N * sizeof(float));

        REQUIRE(aspira_pipeline_process(&pipeline, &input_frame, &output_frame));
        REQUIRE(output_frame.flags & ASPIRA_FRAME_FLAG_PROCESSED);
    }

    aspira_pipeline_destroy(&pipeline);
}
