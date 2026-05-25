#pragma once

#include "../core/event/app_events.h"
#include "lvgl.h"

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
    static CoverService& get();

    uint32_t acceptJpeg(uint8_t* data, uint32_t size);
    CoverState active();
    // Borrowed pointers remain valid only while this cover_id is still active.
    bool borrow(uint32_t cover_id, BorrowedCover* cover);
    void clear();

private:
    CoverService() = default;

    struct CoverEntry {
        uint32_t cover_id = 0;
        CoverStatus status = CoverStatus::Idle;
        uint8_t* jpeg_data = nullptr;
        uint32_t jpeg_size = 0;
        lv_img_dsc_t image{};
        lv_color_t* pixels = nullptr;
    };

    static CoverState stateFromEntry(const CoverEntry& entry);
    void releaseActive();
    void publishChanged(uint32_t cover_id, CoverStatus status);

    mutable std::mutex mutex_;
    uint32_t next_cover_id_ = 0;
    CoverEntry active_{};
};
