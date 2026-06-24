/**
 * @file test_tensor.cpp
 * @brief Unit tests for NCHW tensor data structure
 */

#include <aspira/core/core.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstring>

TEST_CASE("Tensor creation and destruction", "[core][tensor]") {
    SECTION("heap create/free") {
        aspira_tensor* t = aspira_tensor_create(1, 3, 256, 256);
        REQUIRE(t != nullptr);
        REQUIRE(t->n == 1);
        REQUIRE(t->c == 3);
        REQUIRE(t->h == 256);
        REQUIRE(t->w == 256);
        REQUIRE(t->data != nullptr);
        REQUIRE(t->owns_data == true);
        REQUIRE(aspira_tensor_elements(t) == 1 * 3 * 256 * 256);

        aspira_tensor_free(t);
    }

    SECTION("stack init with external data") {
        float data[16] = {0};
        aspira_tensor t;
        aspira_tensor_init(&t, 1, 1, 4, 4, data, false);
        REQUIRE(t.data == data);
        REQUIRE(t.owns_data == false);
        aspira_tensor_destroy(&t);
        /* data should still be valid since we didn't own it */
        REQUIRE(data[0] == 0.0f);
    }
}

TEST_CASE("Tensor fill and copy", "[core][tensor]") {
    aspira_tensor* a = aspira_tensor_create(1, 2, 4, 4);
    aspira_tensor* b = aspira_tensor_create(1, 2, 4, 4);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    SECTION("fill") {
        aspira_tensor_fill(a, 3.14f);
        for (size_t i = 0; i < aspira_tensor_elements(a); i++) {
            REQUIRE(a->data[i] == Catch::Approx(3.14f));
        }
    }

    SECTION("copy") {
        aspira_tensor_fill(a, 7.0f);
        aspira_tensor_copy(a, b);
        REQUIRE(memcmp(a->data, b->data, aspira_tensor_bytes(a)) == 0);
    }

    aspira_tensor_free(a);
    aspira_tensor_free(b);
}

TEST_CASE("Tensor indexing", "[core][tensor]") {
    /* N=1, C=2, H=3, W=4 */
    aspira_tensor* t = aspira_tensor_create(1, 2, 3, 4);
    REQUIRE(t != nullptr);

    /* Fill with known pattern */
    for (uint32_t c = 0; c < 2; c++) {
        for (uint32_t h = 0; h < 3; h++) {
            for (uint32_t w = 0; w < 4; w++) {
                *aspira_tensor_ptr(t, 0, c, h, w) = (float)(c * 100 + h * 10 + w);
            }
        }
    }

    /* Verify indexing */
    REQUIRE(*aspira_tensor_ptr(t, 0, 0, 0, 0) == Catch::Approx(0.0f));
    REQUIRE(*aspira_tensor_ptr(t, 0, 0, 2, 3) == Catch::Approx(23.0f));
    REQUIRE(*aspira_tensor_ptr(t, 0, 1, 1, 1) == Catch::Approx(111.0f));

    /* Verify NCHW layout: channel 1 comes after channel 0 */
    float* ch0_last = aspira_tensor_ptr(t, 0, 0, 2, 3);
    float* ch1_first = aspira_tensor_ptr(t, 0, 1, 0, 0);
    REQUIRE((ch1_first - ch0_last) == 1); /* Adjacent in memory */

    aspira_tensor_free(t);
}

TEST_CASE("Tensor view", "[core][tensor]") {
    aspira_tensor* src = aspira_tensor_create(1, 3, 8, 8);
    REQUIRE(src != nullptr);

    /* Fill with channel-based pattern */
    for (uint32_t c = 0; c < 3; c++) {
        for (uint32_t h = 0; h < 8; h++) {
            for (uint32_t w = 0; w < 8; w++) {
                *aspira_tensor_ptr(src, 0, c, h, w) = (float)c;
            }
        }
    }

    /* Create view of channel 1, rows 2-5, cols 2-5 */
    aspira_tensor view;
    aspira_tensor_view(src, 1, 2, 2, 6, 2, 6, &view);

    REQUIRE(view.n == 1);
    REQUIRE(view.c == 1);
    REQUIRE(view.h == 4);
    REQUIRE(view.w == 4);
    REQUIRE(view.owns_data == false);

    /* View should see channel 1's data at offset */
    REQUIRE(*aspira_tensor_ptr(&view, 0, 0, 0, 0) == Catch::Approx(1.0f));

    /* Destroying view should not free source data */
    aspira_tensor_destroy(&view);
    REQUIRE(src->data != nullptr);

    aspira_tensor_free(src);
}

TEST_CASE("Tensor same shape check", "[core][tensor]") {
    aspira_tensor* a = aspira_tensor_create(1, 3, 64, 64);
    aspira_tensor* b = aspira_tensor_create(1, 3, 64, 64);
    aspira_tensor* c = aspira_tensor_create(1, 3, 32, 32);

    REQUIRE(aspira_tensor_same_shape(a, b));
    REQUIRE_FALSE(aspira_tensor_same_shape(a, c));
    REQUIRE_FALSE(aspira_tensor_same_shape(nullptr, b));

    aspira_tensor_free(a);
    aspira_tensor_free(b);
    aspira_tensor_free(c);
}
