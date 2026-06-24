/**
 * @file inference_scheduler.cpp
 * @brief Inference scheduler implementation
 */

#include "aspira/services/inference_scheduler.h"
#include "aspira/services/logging_service.h"

#include <chrono>
#include <mutex>

namespace aspira {

InferenceScheduler::InferenceScheduler(ThreadPool& pool,
                                        aspira_unet_model* model,
                                        size_t max_concurrent)
    : pool_(pool), model_(model), max_concurrent_(max_concurrent) {}

InferenceScheduler::~InferenceScheduler() {
    wait();
}

void InferenceScheduler::process_one(const InferenceRequest& req,
                                      InferenceCallback cb) {
    auto start = std::chrono::steady_clock::now();

    /* Preprocess: frame → tensor (crop + resize + normalize) */
    aspira_tensor* input = aspira_tensor_create(1, 1, req.target_h, req.target_w);
    if (!input) {
        in_flight_.fetch_sub(1);
        pending_.fetch_sub(1);
        return;
    }

    if (req.frame && req.frame->data) {
        aspira_preprocess_run(req.frame, req.roi_x, req.roi_y,
                               req.roi_w, req.roi_h,
                               req.target_w, req.target_h,
                               req.mean, req.std, input);
    } else {
        /* No frame data — create zero input as fallback */
        aspira_tensor_fill(input, 0.0f);
    }

    /* Inference */
    aspira_unet_forward(model_, input);

    /* Postprocess */
    aspira_segmentation_result result;
    aspira_postprocess_run(model_->output_tensor, req.threshold,
                            req.morph_kernel, &result);

    auto end = std::chrono::steady_clock::now();
    result.inference_us = std::chrono::duration_cast<std::chrono::microseconds>(
        end - start).count();

    total_inference_ns_.fetch_add(result.inference_us * 1000);
    completed_.fetch_add(1);

    /* Callback */
    if (cb) {
        cb(&result, req.user_data);
    }

    aspira_segmentation_result_destroy(&result);
    aspira_tensor_free(input);
    in_flight_.fetch_sub(1);
    pending_.fetch_sub(1);
}

bool InferenceScheduler::submit(const InferenceRequest& req,
                                 InferenceCallback cb) {
    if (in_flight_.load() >= max_concurrent_) {
        return false;  /* Busy */
    }

    pending_.fetch_add(1);
    in_flight_.fetch_add(1);

    /* Capture everything by value for the async task */
    InferenceRequest req_copy = req;
    pool_.enqueue([this, req_copy, cb]() {
        this->process_one(req_copy, cb);
    }, ASPIRA_PRIORITY_INFERENCE);

    return true;
}

double InferenceScheduler::avg_inference_us() const {
    uint64_t c = completed_.load();
    if (c == 0) return 0.0;
    return (double)total_inference_ns_.load() / (double)c / 1000.0;
}

void InferenceScheduler::wait() {
    while (pending_.load() > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

} // namespace aspira
