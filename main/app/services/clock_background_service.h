#pragma once

#include "lvgl.h"
#include "user_config.h"

#include <stddef.h>
#include <stdint.h>

struct ClockForegroundPalette {
    uint32_t fg = 0x22282b;
    uint32_t dim = 0x363b3d;
    uint32_t muted = 0x6f7772;
    uint32_t faint = 0xded7ca;
    uint32_t accent = 0x2f705f;
};

enum class ClockBackgroundStatus : uint8_t {
    Idle,
    Loading,
    Ready,
    Error,
    SizeMismatch,
};

struct ClockBackgroundState {
    uint32_t revision = 0;
    ClockBackgroundStatus status = ClockBackgroundStatus::Idle;
    uint32_t jpeg_size = 0;
    bool has_pixels = false;
    ClockForegroundPalette palette{};
};

struct ClockBackgroundConfig {
    const char* url = CLOCK_BACKGROUND_URL;
    uint32_t refresh_interval_ms = CLOCK_BACKGROUND_REFRESH_INTERVAL_MS;
};

class ClockBackgroundService {
public:
    static constexpr uint16_t kWidth = 640;
    static constexpr uint16_t kHeight = 172;
    static constexpr uint32_t kPixelCount = kWidth * kHeight;
    static constexpr size_t kMaxUrlLength = 256;
    static constexpr uint32_t kDefaultRefreshIntervalMs = 5u * 60u * 1000u;

    static ClockBackgroundService& get();

    void configure(const ClockBackgroundConfig& config);
    ClockBackgroundConfig configSnapshot();
    void requestRefresh();
    bool requestRefreshIfDue();
    bool requestRefreshIfDue(uint32_t now_ms);
    ClockBackgroundState snapshot();
    bool copyPixels(lv_color_t* dst_pixels,
                    uint32_t dst_pixel_count,
                    lv_img_dsc_t* out_image,
                    uint32_t* out_revision);
    void clear();

private:
    ClockBackgroundService();

    static void downloadTask(void* arg);
    void runDownload();
    bool startRefresh(uint32_t now_ms);
    bool acceptJpeg(uint8_t* jpeg_data, uint32_t jpeg_size,
                    const ClockForegroundPalette& palette);
    void releasePixels();

    char url_[kMaxUrlLength] = {};
    uint32_t refresh_interval_ms_ = kDefaultRefreshIntervalMs;
    uint32_t last_request_ms_ = 0;
    uint32_t revision_ = 0;
    ClockBackgroundStatus status_ = ClockBackgroundStatus::Idle;
    uint32_t jpeg_size_ = 0;
    lv_color_t* pixels_ = nullptr;
    ClockForegroundPalette palette_{};
    bool loading_ = false;
    bool refresh_due_immediately_ = true;
};
