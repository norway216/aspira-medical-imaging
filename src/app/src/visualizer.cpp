/**
 * @file visualizer.cpp
 * @brief OpenCV-based visualization implementation
 */

#include "aspira/app/visualizer.h"

#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <sstream>
#include <iomanip>

namespace aspira {

/* ==========================================================================
 * Internal: convert float frame to cv::Mat
 * ========================================================================== */

static cv::Mat frame_to_mat(const aspira_frame* frame, int disp_w, int disp_h) {
    if (!frame || !frame->data) return cv::Mat();

    int fw = (int)frame->width;
    int fh = (int)frame->height;
    size_t total = (size_t)fw * fh;

    /* Find min/max for auto-normalization */
    float fmin = frame->data[0], fmax = frame->data[0];
    for (size_t i = 1; i < total; i++) {
        float v = frame->data[i];
        if (v < fmin) fmin = v;
        if (v > fmax) fmax = v;
    }
    float range = fmax - fmin;
    if (range < 1e-10f) range = 1.0f;

    /* Convert to 8-bit grayscale */
    cv::Mat gray(fh, fw, CV_8UC1);
    for (int y = 0; y < fh; y++) {
        uint8_t* row = gray.ptr<uint8_t>(y);
        const float* src = frame->data + (size_t)y * fw;
        for (int x = 0; x < fw; x++) {
            float val = (src[x] - fmin) / range * 255.0f;
            if (val < 0.0f) val = 0.0f;
            if (val > 255.0f) val = 255.0f;
            row[x] = (uint8_t)val;
        }
    }

    /* Resize if display size specified */
    if (disp_w > 0 && disp_h > 0 && (disp_w != fw || disp_h != fh)) {
        cv::Mat resized;
        cv::resize(gray, resized, cv::Size(disp_w, disp_h),
                    0, 0, cv::INTER_LINEAR);
        return resized;
    }

    return gray;
}

/* ==========================================================================
 * Visualizer Implementation
 * ========================================================================== */

Visualizer::Visualizer(const std::string& window_name, int width, int height)
    : window_name_(window_name), display_w_(width), display_h_(height) {}

Visualizer::~Visualizer() {
    close();
}

void Visualizer::ensure_window() {
    if (!created_) {
        cv::namedWindow(window_name_, cv::WINDOW_NORMAL | cv::WINDOW_KEEPRATIO);
        if (display_w_ > 0 && display_h_ > 0) {
            cv::resizeWindow(window_name_, display_w_, display_h_);
        }
        created_ = true;
    }
}

void Visualizer::show(const aspira_frame* frame, const std::string& title) {
    ensure_window();
    if (!frame || !frame->data) return;

    cv::Mat img = frame_to_mat(frame, display_w_, display_h_);
    if (img.empty()) return;

    /* Apply color map for better contrast (ultrasound-like) */
    cv::Mat color;
    cv::applyColorMap(img, color, cv::COLORMAP_BONE);

    /* Draw status bar at bottom */
    if (!status_text_.empty()) {
        int bar_h = 24;
        cv::Mat with_bar(color.rows + bar_h, color.cols, CV_8UC3,
                          cv::Scalar(20, 20, 30));
        color.copyTo(with_bar(cv::Rect(0, 0, color.cols, color.rows)));
        cv::putText(with_bar, status_text_, cv::Point(8, color.rows + 17),
                    cv::FONT_HERSHEY_SIMPLEX, 0.45,
                    cv::Scalar(200, 200, 200), 1);
        color = with_bar;
    }

    /* Title overlay */
    if (!title.empty()) {
        cv::putText(color, title, cv::Point(8, 20),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    cv::Scalar(0, 255, 0), 1);
    }

    cv::imshow(window_name_, color);
}

void Visualizer::show_dual(const aspira_frame* left, const std::string& left_label,
                            const aspira_frame* right, const std::string& right_label) {
    ensure_window();
    if (!left || !right || !left->data || !right->data) return;

    /* Use half display width for each */
    int half_w = (display_w_ > 0) ? display_w_ / 2 : 0;
    int half_h = display_h_;

    cv::Mat left_img = frame_to_mat(left, half_w, half_h);
    cv::Mat right_img = frame_to_mat(right, half_w, half_h);

    if (left_img.empty() || right_img.empty()) return;

    /* Apply colormap */
    cv::Mat left_color, right_color;
    cv::applyColorMap(left_img, left_color, cv::COLORMAP_BONE);
    cv::applyColorMap(right_img, right_color, cv::COLORMAP_BONE);

    /* Ensure same height */
    int h = std::max(left_color.rows, right_color.rows);
    if (left_color.rows < h) cv::copyMakeBorder(left_color, left_color, 0, h - left_color.rows, 0, 0, cv::BORDER_CONSTANT, cv::Scalar(0,0,0));
    if (right_color.rows < h) cv::copyMakeBorder(right_color, right_color, 0, h - right_color.rows, 0, 0, cv::BORDER_CONSTANT, cv::Scalar(0,0,0));

    /* Labels */
    if (!left_label.empty())
        cv::putText(left_color, left_label, cv::Point(8, 16),
                    cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 255, 0), 1);
    if (!right_label.empty())
        cv::putText(right_color, right_label, cv::Point(8, 16),
                    cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 255, 0), 1);

    /* Concatenate side by side */
    cv::Mat dual;
    cv::hconcat(left_color, right_color, dual);

    /* Status bar */
    if (!status_text_.empty()) {
        int bar_h = 24;
        cv::Mat with_bar(dual.rows + bar_h, dual.cols, CV_8UC3,
                          cv::Scalar(20, 20, 30));
        dual.copyTo(with_bar(cv::Rect(0, 0, dual.cols, dual.rows)));
        cv::putText(with_bar, status_text_, cv::Point(8, dual.rows + 17),
                    cv::FONT_HERSHEY_SIMPLEX, 0.4,
                    cv::Scalar(200, 200, 200), 1);
        dual = with_bar;
    }

    cv::imshow(window_name_, dual);
}

void Visualizer::set_status(const std::string& text) {
    status_text_ = text;
}

int Visualizer::wait_key(int delay_ms) {
    return cv::waitKey(delay_ms);
}

bool Visualizer::is_open() const {
    if (!created_) return true;
    return cv::getWindowProperty(window_name_, cv::WND_PROP_VISIBLE) >= 0;
}

void Visualizer::close() {
    if (created_) {
        cv::destroyWindow(window_name_);
        created_ = false;
    }
}

void Visualizer::set_display_size(int w, int h) {
    display_w_ = w;
    display_h_ = h;
    if (created_) {
        cv::resizeWindow(window_name_, w, h);
    }
}

} // namespace aspira
