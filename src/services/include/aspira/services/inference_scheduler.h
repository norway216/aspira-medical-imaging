/**
 * @file inference_scheduler.h
 * @brief Queue-based AI inference request scheduler
 *
 * Manages a queue of inference requests, dispatching them to the
 * thread pool at ASPIRA_PRIORITY_INFERENCE level with configurable
 * concurrency limits.
 */

#ifndef ASPIRA_SERVICES_INFERENCE_SCHEDULER_H
#define ASPIRA_SERVICES_INFERENCE_SCHEDULER_H

#include <aspira/core/core.h>
#include <aspira/services/module_wrapper.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace aspira {

struct InferenceRequest {
    aspira_frame* frame = nullptr;
    uint32_t roi_x = 0, roi_y = 0, roi_w = 0, roi_h = 0;
    float threshold = 0.5f;
    float mean = 0.5f;
    float std = 0.5f;
    uint32_t morph_kernel = 3;
    uint32_t target_w = 256;
    uint32_t target_h = 256;
    void* user_data = nullptr;
};

using InferenceCallback = std::function<void(
    const aspira_segmentation_result* result, void* user_data)>;

class InferenceScheduler {
public:
    InferenceScheduler(ThreadPool& pool, aspira_unet_model* model,
                       size_t max_concurrent = 2);
    ~InferenceScheduler();

    InferenceScheduler(const InferenceScheduler&) = delete;
    InferenceScheduler& operator=(const InferenceScheduler&) = delete;

    /**
     * @brief Submit an inference request (non-blocking)
     * @return true if queued, false if queue full
     */
    bool submit(const InferenceRequest& req, InferenceCallback cb);

    /**
     * @brief Get number of pending requests
     */
    size_t pending() const { return pending_.load(); }

    /**
     * @brief Get total completed inferences
     */
    uint64_t completed() const { return completed_.load(); }

    /**
     * @brief Get average inference time in microseconds
     */
    double avg_inference_us() const;

    /**
     * @brief Wait for all pending requests to complete
     */
    void wait();

private:
    ThreadPool& pool_;
    aspira_unet_model* model_;
    size_t max_concurrent_;
    std::atomic<size_t> pending_{0};
    std::atomic<size_t> in_flight_{0};
    std::atomic<uint64_t> completed_{0};
    std::atomic<uint64_t> total_inference_ns_{0};

    void process_one(const InferenceRequest& req, InferenceCallback cb);
};

} // namespace aspira

#endif
