/**
 * @file visualizer.h
 * @brief OpenCV-based real-time visualization window for debugging
 *
 * Displays frame data in an OpenCV window with FPS overlay,
 * dual-view (raw + processed), and keyboard controls.
 */

#ifndef ASPIRA_APP_VISUALIZER_H
#define ASPIRA_APP_VISUALIZER_H

#include <aspira/core/core.h>
#include <string>

namespace aspira {

class Visualizer {
public:
    /**
     * @param window_name  Window title
     * @param width        Display width (0 = auto from frame)
     * @param height       Display height (0 = auto from frame)
     */
    explicit Visualizer(const std::string& window_name = "Aspira Imaging",
                        int width = 0, int height = 0);
    ~Visualizer();

    Visualizer(const Visualizer&) = delete;
    Visualizer& operator=(const Visualizer&) = delete;

    /**
     * @brief Show a single frame
     * @param frame     Frame to display (float data, auto-normalized to [0,255])
     * @param title     Optional overlay label
     */
    void show(const aspira_frame* frame, const std::string& title = "");

    /**
     * @brief Show two frames side by side (e.g., raw + processed)
     */
    void show_dual(const aspira_frame* left, const std::string& left_label,
                   const aspira_frame* right, const std::string& right_label);

    /**
     * @brief Overlay status text (FPS, frame number, latency, etc.)
     */
    void set_status(const std::string& text);

    /**
     * @brief Wait for keyboard input (non-blocking with small delay)
     * @param delay_ms  Wait time in ms (1 = non-blocking, 0 = wait forever)
     * @return Key code pressed, or -1 if no key
     */
    int wait_key(int delay_ms = 1);

    /**
     * @brief Check if window is still open
     */
    bool is_open() const;

    /**
     * @brief Close the window
     */
    void close();

    /**
     * @brief Set display size (resize frame to this size for display)
     */
    void set_display_size(int w, int h);

private:
    std::string window_name_;
    int display_w_ = 0;
    int display_h_ = 0;
    bool created_ = false;
    std::string status_text_;

    void ensure_window();
    void draw_status_bar(void* mat_ptr);
};

} // namespace aspira

#endif
