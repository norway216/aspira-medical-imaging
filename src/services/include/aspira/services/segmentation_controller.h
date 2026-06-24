/**
 * @file segmentation_controller.h
 * @brief AI segmentation lifecycle manager with security and workflow integration
 */

#ifndef ASPIRA_SERVICES_SEGMENTATION_CONTROLLER_H
#define ASPIRA_SERVICES_SEGMENTATION_CONTROLLER_H

#include <aspira/core/core.h>

#include <functional>
#include <string>

namespace aspira {

class InferenceScheduler;
class SecurityManager;
class ThreadPool;
class LoggingService;

class SegmentationController {
public:
    /**
     * @param scheduler  Inference scheduler for async execution
     * @param security   Security manager for permission checks
     * @param logger     Logger for diagnostics
     */
    SegmentationController(InferenceScheduler& scheduler,
                           SecurityManager& security,
                           LoggingService* logger = nullptr);
    ~SegmentationController() = default;

    /**
     * @brief Start AI segmentation (checks permissions)
     * @return true if started successfully
     */
    bool start();

    /**
     * @brief Stop AI segmentation
     */
    void stop();

    /**
     * @brief Set region of interest for segmentation
     */
    void set_roi(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

    /**
     * @brief Clear ROI (use full frame)
     */
    void clear_roi();

    /**
     * @brief Set post-processing parameters
     */
    void set_threshold(float t) { threshold_ = t; }
    void set_morph_kernel(uint32_t k) { morph_kernel_ = k; }
    void set_model_input_size(uint32_t w, uint32_t h) {
        target_w_ = w; target_h_ = h;
    }
    void set_normalization(float mean, float std) {
        mean_ = mean; std_ = std;
    }

    /**
     * @brief Get current settings
     */
    float threshold() const { return threshold_; }
    uint32_t morph_kernel() const { return morph_kernel_; }
    bool has_roi() const { return has_roi_; }
    bool is_running() const { return running_; }

private:
    InferenceScheduler& scheduler_;
    SecurityManager& security_;
    LoggingService* logger_;

    bool running_ = false;
    bool has_roi_ = false;
    uint32_t roi_x_ = 0, roi_y_ = 0, roi_w_ = 0, roi_h_ = 0;
    float threshold_ = 0.5f;
    uint32_t morph_kernel_ = 3;
    uint32_t target_w_ = 256, target_h_ = 256;
    float mean_ = 0.5f, std_ = 0.5f;
};

} // namespace aspira

#endif
