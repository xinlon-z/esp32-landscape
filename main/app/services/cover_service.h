#pragma once

#include "../core/event/app_events.h"
#include "lvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <mutex>
#include <stdint.h>

struct CoverState {
    uint32_t cover_id = 0;
    CoverStatus status = CoverStatus::Idle;
    uint32_t jpeg_size = 0;
    bool has_jpeg = false;
    bool has_pixels = false;
};

struct BorrowedCover {
    uint32_t cover_id = 0;
    CoverStatus status = CoverStatus::Idle;
    const uint8_t* jpeg_data = nullptr;
    uint32_t jpeg_size = 0;
    const lv_img_dsc_t* image = nullptr;
    const lv_color_t* pixels = nullptr;
};

class CoverService {
public:
    static constexpr uint16_t kCoverSize = 144;
    static constexpr uint32_t kCoverPixelCount = kCoverSize * kCoverSize;

    static CoverService& get();

    // Queue a JPEG for async decoding on Core 1. Returns the cover_id assigned
    // to this JPEG immediately without blocking. CoverStateChanged{Loading} is
    // published synchronously; CoverStateChanged{Ready/Error} follows once the
    // Core 1 worker finishes the decode (~250 ms uncontested).
    uint32_t acceptJpeg(uint8_t* data, uint32_t size);
    CoverState active();
    // Borrowed pointers remain valid only while this cover_id is still active.
    bool borrow(uint32_t cover_id, BorrowedCover* cover);
    bool copyPixels(uint32_t cover_id,
                    lv_color_t* dst_pixels,
                    uint32_t dst_pixel_count,
                    lv_img_dsc_t* out_image);
    void clear();

    // Testing support: drives one pending decode synchronously on the caller's
    // thread. In production the Core 1 worker calls this; tests use it to avoid
    // the no-op xTaskCreatePinnedToCore stub and verify post-decode state.
    bool tickDecodeForTest() { return runOnePendingDecode(); }

private:
    CoverService();

    struct CoverEntry {
        uint32_t cover_id = 0;
        CoverStatus status = CoverStatus::Idle;
        uint8_t* jpeg_data = nullptr;
        uint32_t jpeg_size = 0;
        lv_img_dsc_t image{};
        lv_color_t* pixels = nullptr;
    };

    struct PendingDecode {
        uint8_t* jpeg_data = nullptr;
        uint32_t jpeg_size = 0;
        uint32_t cover_id = 0;
    };

    static CoverState stateFromEntry(const CoverEntry& entry);
    void releaseActive();
    void freePendingDecode();
    void ensureDecodeWorkerStarted();

    // Pops one pending decode job, runs it, and publishes the result.
    // Called by the worker loop; also reachable from unit tests via
    // the #define private public shim.
    bool runOnePendingDecode();

    static void decodeWorkerTask(void* arg);
    void decodeWorkerLoop();
    void publishChanged(uint32_t cover_id, CoverStatus status);

    mutable std::mutex mutex_;
    uint32_t next_cover_id_ = 0;
    CoverEntry active_{};

    SemaphoreHandle_t decode_signal_ = nullptr;
    bool decode_worker_started_ = false;
    PendingDecode pending_{};
    bool has_pending_ = false;
};
