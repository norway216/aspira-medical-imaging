/**
 * @file test_unet.cpp
 * @brief Unit tests for U-Net CPU inference engine
 */

#include <aspira/core/cpp_compat.h>
#include <aspira/core/unet.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstring>

/* ==========================================================================
 * Individual Layer Tests
 * ========================================================================== */

TEST_CASE("Conv2D with identity kernel", "[core][unet][conv2d]") {
    /* 1×1×4×4 input, 1 output channel, 3×3 identity kernel */
    aspira_tensor* input = aspira_tensor_create(1, 1, 4, 4);
    aspira_tensor_fill(input, 0.0f);
    *aspira_tensor_ptr(input, 0, 0, 1, 1) = 1.0f;
    *aspira_tensor_ptr(input, 0, 0, 2, 2) = 2.0f;

    aspira_conv2d_params params;
    params.in_channels = 1;
    params.out_channels = 1;
    params.kernel_size = 3;
    params.stride = 1;
    params.padding = 1;

    /* Identity kernel: center=1, rest=0 */
    float w[9] = {0,0,0, 0,1,0, 0,0,0};
    float b[1] = {0.0f};
    params.weights = w;
    params.bias = b;

    aspira_tensor* output = aspira_tensor_create(1, 1, 4, 4);
    aspira_unet_conv2d(input, &params, output);

    /* Output should match input (identity kernel + zero bias) */
    REQUIRE(*aspira_tensor_ptr(output, 0, 0, 1, 1) == Catch::Approx(1.0f));
    REQUIRE(*aspira_tensor_ptr(output, 0, 0, 2, 2) == Catch::Approx(2.0f));

    aspira_tensor_free(input);
    aspira_tensor_free(output);
}

TEST_CASE("ReLU activation", "[core][unet][relu]") {
    aspira_tensor* t = aspira_tensor_create(1, 2, 1, 3);
    t->data[0] = -1.0f; t->data[1] = 0.0f; t->data[2] = 1.0f;
    t->data[3] = -0.5f; t->data[4] = 2.0f; t->data[5] = -100.0f;

    aspira_unet_relu(t);

    REQUIRE(t->data[0] == 0.0f);
    REQUIRE(t->data[1] == 0.0f);
    REQUIRE(t->data[2] == 1.0f);
    REQUIRE(t->data[3] == 0.0f);
    REQUIRE(t->data[4] == 2.0f);
    REQUIRE(t->data[5] == 0.0f);

    aspira_tensor_free(t);
}

TEST_CASE("MaxPool2D", "[core][unet][maxpool]") {
    /* 1×1×4×4 input */
    aspira_tensor* input = aspira_tensor_create(1, 1, 4, 4);
    float vals[16] = {1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16};
    memcpy(input->data, vals, sizeof(vals));

    aspira_tensor* output = aspira_tensor_create(1, 1, 2, 2);
    aspira_unet_maxpool2d(input, 2, 2, output);

    /* 2x2 max pooling: max of each 2x2 quadrant */
    REQUIRE(output->data[0] == 6.0f);   /* max(1,2,5,6) */
    REQUIRE(output->data[1] == 8.0f);   /* max(3,4,7,8) */
    REQUIRE(output->data[2] == 14.0f);  /* max(9,10,13,14) */
    REQUIRE(output->data[3] == 16.0f);  /* max(11,12,15,16) */

    aspira_tensor_free(input);
    aspira_tensor_free(output);
}

TEST_CASE("UpSample nearest-neighbor", "[core][unet][upsample]") {
    aspira_tensor* input = aspira_tensor_create(1, 1, 2, 2);
    input->data[0] = 1.0f; input->data[1] = 2.0f;
    input->data[2] = 3.0f; input->data[3] = 4.0f;

    aspira_tensor* output = aspira_tensor_create(1, 1, 4, 4);
    aspira_unet_upsample(input, 2, output);

    /* Each pixel replicated to 2x2 block:
       1→(0,0)-(1,1), 2→(0,2)-(1,3), 3→(2,0)-(3,1), 4→(2,2)-(3,3) */
    REQUIRE(*aspira_tensor_ptr(output, 0, 0, 0, 0) == 1.0f);
    REQUIRE(*aspira_tensor_ptr(output, 0, 0, 0, 1) == 1.0f);
    REQUIRE(*aspira_tensor_ptr(output, 0, 0, 0, 2) == 2.0f);
    REQUIRE(*aspira_tensor_ptr(output, 0, 0, 2, 0) == 3.0f);
    REQUIRE(*aspira_tensor_ptr(output, 0, 0, 3, 3) == 4.0f);

    aspira_tensor_free(input);
    aspira_tensor_free(output);
}

TEST_CASE("Concat along channels", "[core][unet][concat]") {
    aspira_tensor* a = aspira_tensor_create(1, 2, 2, 2);
    aspira_tensor* b = aspira_tensor_create(1, 3, 2, 2);
    /* Fill a with 1s, b with 2s */
    aspira_tensor_fill(a, 1.0f);
    aspira_tensor_fill(b, 2.0f);

    aspira_tensor* output = aspira_tensor_create(1, 5, 2, 2);
    aspira_unet_concat(a, b, output);

    /* First 2 channels should be from a (1s), last 3 from b (2s) */
    REQUIRE(*aspira_tensor_ptr(output, 0, 0, 0, 0) == 1.0f);
    REQUIRE(*aspira_tensor_ptr(output, 0, 1, 0, 0) == 1.0f);
    REQUIRE(*aspira_tensor_ptr(output, 0, 2, 0, 0) == 2.0f);
    REQUIRE(*aspira_tensor_ptr(output, 0, 4, 0, 0) == 2.0f);

    aspira_tensor_free(a);
    aspira_tensor_free(b);
    aspira_tensor_free(output);
}

TEST_CASE("Sigmoid activation", "[core][unet][sigmoid]") {
    aspira_tensor* t = aspira_tensor_create(1, 1, 1, 4);
    t->data[0] = 0.0f;
    t->data[1] = 10.0f;
    t->data[2] = -10.0f;
    t->data[3] = 2.0f;

    aspira_unet_sigmoid(t);

    REQUIRE(t->data[0] == Catch::Approx(0.5f));
    REQUIRE(t->data[1] == Catch::Approx(1.0f).margin(0.001f));
    REQUIRE(t->data[2] == Catch::Approx(0.0f).margin(0.001f));
    REQUIRE(t->data[3] > 0.8f);

    aspira_tensor_free(t);
}

/* ==========================================================================
 * Model-level Tests
 * ========================================================================== */

TEST_CASE("U-Net model create and destroy", "[core][unet][model]") {
    aspira_unet_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.input_width = 256;
    cfg.input_height = 256;
    cfg.input_channels = 1;
    cfg.base_channels = 32;
    cfg.num_encoder_blocks = 4;
    cfg.num_classes = 1;

    aspira_unet_model* model = aspira_unet_create(&cfg);
    REQUIRE(model != nullptr);

    size_t params = aspira_unet_param_count(model);
    REQUIRE(params > 1000000);  /* Should be millions of params */
    REQUIRE(params < 10000000); /* Should be <10M */

    aspira_unet_free(model);
}

TEST_CASE("U-Net forward pass produces valid output", "[core][unet][forward]") {
    /* Tiny model: 64x64, 2 encoder blocks, base=8 */
    aspira_unet_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.input_width = 64;
    cfg.input_height = 64;
    cfg.input_channels = 1;
    cfg.base_channels = 8;
    cfg.num_encoder_blocks = 2;
    cfg.num_classes = 1;

    aspira_unet_model* model = aspira_unet_create(&cfg);
    REQUIRE(model != nullptr);

    /* Create random input */
    aspira_tensor* input = aspira_tensor_create(1, 1, 64, 64);
    for (size_t i = 0; i < aspira_tensor_elements(input); i++) {
        input->data[i] = (float)rand() / (float)RAND_MAX;
    }

    /* Run forward pass — should not crash */
    bool ok = aspira_unet_forward(model, input);
    REQUIRE(ok);

    /* Check output shape */
    REQUIRE(model->output_tensor != nullptr);
    REQUIRE(model->output_tensor->h == 64);
    REQUIRE(model->output_tensor->w == 64);

    /* First value should be valid */
    REQUIRE(model->output_tensor->data[0] >= 0.0f);
    REQUIRE(model->output_tensor->data[0] <= 1.0f);

    aspira_tensor_free(input);
    aspira_unet_free(model);
}
