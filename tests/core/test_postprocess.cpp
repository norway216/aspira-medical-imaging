/**
 * @file test_postprocess.cpp
 * @brief Unit tests for AI post-processing pipeline
 */

#include <aspira/core/core.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstring>

TEST_CASE("Threshold", "[core][postprocess][threshold]") {
    aspira_tensor* src = aspira_tensor_create(1, 1, 1, 6);
    src->data[0] = 0.1f; src->data[1] = 0.49f;
    src->data[2] = 0.5f; src->data[3] = 0.51f;
    src->data[4] = 0.9f; src->data[5] = 1.0f;

    aspira_tensor* dst = aspira_tensor_create(1, 1, 1, 6);
    aspira_postprocess_threshold(src, 0.5f, dst);

    REQUIRE(dst->data[0] == 0.0f);
    REQUIRE(dst->data[1] == 0.0f);
    REQUIRE(dst->data[2] == 1.0f);  /* 0.5 >= 0.5 → 1 */
    REQUIRE(dst->data[3] == 1.0f);
    REQUIRE(dst->data[4] == 1.0f);
    REQUIRE(dst->data[5] == 1.0f);

    aspira_tensor_free(src);
    aspira_tensor_free(dst);
}

TEST_CASE("Erosion", "[core][postprocess][erode]") {
    /* 5x5 mask with a single 0 in the center of all 1s */
    aspira_tensor* mask = aspira_tensor_create(1, 1, 5, 5);
    aspira_tensor_fill(mask, 1.0f);
    mask->data[2 * 5 + 2] = 0.0f;  /* Center pixel = 0 */

    /* 3x3 erosion should expand the 0 */
    aspira_postprocess_erode(mask, 3);

    /* The center 3x3 should now be 0 (anyone with a 0 neighbor in 3x3) */
    for (int y = 1; y <= 3; y++)
        for (int x = 1; x <= 3; x++)
            REQUIRE(mask->data[y * 5 + x] == 0.0f);

    /* Corners should still be 1 */
    REQUIRE(mask->data[0] == 1.0f);
    REQUIRE(mask->data[4] == 1.0f);

    aspira_tensor_free(mask);
}

TEST_CASE("Dilation", "[core][postprocess][dilate]") {
    /* 5x5 mask with a single 1 in the center of all 0s */
    aspira_tensor* mask = aspira_tensor_create(1, 1, 5, 5);
    aspira_tensor_fill(mask, 0.0f);
    mask->data[2 * 5 + 2] = 1.0f;

    /* 3x3 dilation should expand the 1 */
    aspira_postprocess_dilate(mask, 3);

    /* The center 3x3 should now be 1 */
    for (int y = 1; y <= 3; y++)
        for (int x = 1; x <= 3; x++)
            REQUIRE(mask->data[y * 5 + x] == 1.0f);

    /* Corners should still be 0 */
    REQUIRE(mask->data[0] == 0.0f);

    aspira_tensor_free(mask);
}

TEST_CASE("Confidence", "[core][postprocess][confidence]") {
    aspira_tensor* prob = aspira_tensor_create(1, 1, 2, 2);
    prob->data[0] = 0.2f; prob->data[1] = 0.4f;
    prob->data[2] = 0.6f; prob->data[3] = 0.8f;

    float conf = aspira_postprocess_confidence(prob);
    REQUIRE(conf == Catch::Approx(0.5f));

    aspira_tensor_free(prob);
}

TEST_CASE("Full postprocess pipeline", "[core][postprocess][integration]") {
    /* Synthetic sigmoid output: 8x8 with a clear "lesion" in center */
    aspira_tensor* raw = aspira_tensor_create(1, 1, 8, 8);
    aspira_tensor_fill(raw, 0.1f);
    /* Create a 4x4 high-probability region in center */
    for (int y = 2; y < 6; y++)
        for (int x = 2; x < 6; x++)
            raw->data[y * 8 + x] = 0.9f;

    aspira_segmentation_result result;
    bool ok = aspira_postprocess_run(raw, 0.5f, 3, &result);
    REQUIRE(ok);
    REQUIRE(result.mask != nullptr);
    REQUIRE(result.width == 8);
    REQUIRE(result.height == 8);
    REQUIRE(result.confidence > 0.0f);

    /* Center pixels should be 1 after threshold */
    REQUIRE(result.mask[4 * 8 + 4] == 1.0f);
    /* Corner pixels should be 0 */
    REQUIRE(result.mask[0] == 0.0f);

    aspira_segmentation_result_destroy(&result);
    aspira_tensor_free(raw);
}
