/**
 * @file segmentation_controller.cpp
 * @brief Segmentation controller implementation
 */

#include "aspira/services/segmentation_controller.h"
#include "aspira/services/inference_scheduler.h"
#include "aspira/services/security_manager.h"
#include "aspira/services/logging_service.h"

namespace aspira {

SegmentationController::SegmentationController(
    InferenceScheduler& scheduler,
    SecurityManager& security,
    LoggingService* logger)
    : scheduler_(scheduler)
    , security_(security)
    , logger_(logger) {}

bool SegmentationController::start() {
    if (!security_.authorize(Permission::START_SCAN)) {
        if (logger_) logger_->warn("AI segmentation denied: insufficient permissions");
        return false;
    }

    if (running_) return true;

    running_ = true;
    if (logger_) logger_->info("AI segmentation started");
    return true;
}

void SegmentationController::stop() {
    running_ = false;
    if (logger_) logger_->info("AI segmentation stopped");
}

void SegmentationController::set_roi(uint32_t x, uint32_t y,
                                      uint32_t w, uint32_t h) {
    roi_x_ = x; roi_y_ = y;
    roi_w_ = w; roi_h_ = h;
    has_roi_ = true;
}

void SegmentationController::clear_roi() {
    has_roi_ = false;
    roi_x_ = roi_y_ = roi_w_ = roi_h_ = 0;
}

} // namespace aspira
