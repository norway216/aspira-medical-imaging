/**
 * @file test_preprocess.cpp
 * @brief Unit tests for AI pre-processing pipeline
 */

#include <aspira/core/core.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstring>

TEST_CASE("ROI crop from frame to tensor", "[core][preprocess][crop]") {
    /* Create a 10-wide, 8-tall frame with known pattern */
    const uint32_t W = 10, H = 8;
    float* frame_data = (float*)calloc(W * H, sizeof(float));
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++)
            frame_data[y * W + x] = (float)(y * 100 + x);

    aspira_frame frame;
    aspira_frame_init(&frame, W, H, 1, frame_data, W * H * sizeof(float));

    SECTION("full frame crop (ROI=0)") {
        aspira_tensor* dst = aspira_tensor_create(1, 1, H, W);
        aspira_preprocess_roi_crop(&frame, 0, 0, 0, 0, dst);
        REQUIRE(dst->data[0] == Catch::Approx(0.0f));      /* (0,0) */
        REQUIRE(dst->data[W*7 + 9] == Catch::Approx(709.0f)); /* (7,9) */
        aspira_tensor_free(dst);
    }

    SECTION("sub-region crop") {
        /* Crop 4x3 region starting at (2, 3) */
        aspira_tensor* dst = aspira_tensor_create(1, 1, 3, 4);
        aspira_preprocess_roi_crop(&frame, 2, 3, 4, 3, dst);
        /* First pixel of ROI should be frame(3,2) = 3*100+2 = 302 */
        REQUIRE(dst->data[0] == Catch::Approx(302.0f));
        aspira_tensor_free(dst);
    }

    free(frame_data);
}

TEST_CASE("Bilinear resize", "[core][preprocess][resize]") {
    /* 2x2 source: [[1,2],[3,4]] */
    aspira_tensor* src = aspira_tensor_create(1, 1, 2, 2);
    src->data[0] = 1.0f; src->data[1] = 2.0f;
    src->data[2] = 3.0f; src->data[3] = 4.0f;

    SECTION("upscale 2x2 to 4x4") {
        aspira_tensor* dst = aspira_tensor_create(1, 1, 4, 4);
        aspira_preprocess_resize(src, 4, 4, dst);

        /* Corners should match source exactly */
        REQUIRE(*aspira_tensor_ptr(dst, 0, 0, 0, 0) == Catch::Approx(1.0f));
        REQUIRE(*aspira_tensor_ptr(dst, 0, 0, 0, 3) == Catch::Approx(2.0f));
        REQUIRE(*aspira_tensor_ptr(dst, 0, 0, 3, 0) == Catch::Approx(3.0f));
        REQUIRE(*aspira_tensor_ptr(dst, 0, 0, 3, 3) == Catch::Approx(4.0f));

        /* Center should be interpolated */
        float center = *aspira_tensor_ptr(dst, 0, 0, 1, 1);
        REQUIRE(center >= 1.0f);
        REQUIRE(center <= 4.0f);

        aspira_tensor_free(dst);
    }

    SECTION("downscale 4x4 to 2x2") {
        aspira_tensor* big = aspira_tensor_create(1, 1, 4, 4);
        /* Fill with gradient */
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                *aspira_tensor_ptr(big, 0, 0, y, x) = (float)(y * 4 + x);

        aspira_tensor* small = aspira_tensor_create(1, 1, 2, 2);
        aspira_preprocess_resize(big, 2, 2, small);

        /* Corners should match */
        REQUIRE(small->data[0] == Catch::Approx(0.0f));
        REQUIRE(small->data[3] == Catch::Approx(15.0f));

        aspira_tensor_free(big);
        aspira_tensor_free(small);
    }

    aspira_tensor_free(src);
}

TEST_CASE("Normalize", "[core][preprocess][normalize]") {
    aspira_tensor* t = aspira_tensor_create(1, 1, 1, 4);
    t->data[0] = 0.0f;
    t->data[1] = 0.5f;
    t->data[2] = 1.0f;
    t->data[3] = 2.0f;

    /* mean=0.5, std=0.5: [0,0.5,1,2] -> [-1,0,1,3] */
    aspira_preprocess_normalize(t, 0.5f, 0.5f);

    REQUIRE(t->data[0] == Catch::Approx(-1.0f));
    REQUIRE(t->data[1] == Catch::Approx(0.0f));
    REQUIRE(t->data[2] == Catch::Approx(1.0f));
    REQUIRE(t->data[3] == Catch::Approx(3.0f));

    aspira_tensor_free(t);
}

TEST_CASE("Full preprocess pipeline", "[core][preprocess][integration]") {
    /* Create a 32x32 test frame */
    float* fd = (float*)calloc(32 * 32, sizeof(float));
    for (int i = 0; i < 32 * 32; i++) fd[i] = (float)(i % 256) / 255.0f;

    aspira_frame frame;
    aspira_frame_init(&frame, 32, 32, 1, fd, 32 * 32 * sizeof(float));

    aspira_tensor* output = aspira_tensor_create(1, 1, 16, 16);
    bool ok = aspira_preprocess_run(&frame, 0, 0, 0, 0, 16, 16, 0.5f, 0.5f, output);
    REQUIRE(ok);
    REQUIRE(output->w == 16);
    REQUIRE(output->h == 16);
    /* Values should be normalized (roughly [-1, 1]) */
    REQUIRE(output->data[0] >= -2.0f);
    REQUIRE(output->data[0] <= 2.0f);

    aspira_tensor_free(output);
    free(fd);
}
