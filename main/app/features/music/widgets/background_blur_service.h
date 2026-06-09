#pragma once

#include "background_image.h"
#include "lvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdint.h>

class BackgroundBlurService {
public:
    using ReadyCallback = void (*)(uint32_t cover_id, void* user_data);

    static BackgroundBlurService& get();

    bool tryGetCached(uint32_t cover_id, const lv_img_dsc_t** out_image);

    bool request(uint32_t cover_id,
                 const lv_img_dsc_t& source_image,
                 const lv_color_t* source_pixels,
                 uint16_t target_w,
                 uint16_t target_h,
                 const lv_img_dsc_t** out_image);

    void setReadyCallback(ReadyCallback callback, void* user_data);
    void clearReadyCallback(void* user_data);

private:
    BackgroundBlurService();

    struct PendingJob {
        uint32_t cover_id = 0;
        lv_color_t* src_pixels = nullptr;
        uint16_t src_w = 0;
        uint16_t src_h = 0;
        uint16_t target_w = 0;
        uint16_t target_h = 0;
    };

    void ensureWorkerStarted();
    static void taskTrampoline(void* arg);
    void taskLoop();
    static void onAsyncReady(void* user_data);
    static void onStaleTimer(lv_timer_t* timer);
    void freeJob(PendingJob* job);

    SemaphoreHandle_t mutex_ = nullptr;
    SemaphoreHandle_t signal_ = nullptr;
    bool worker_started_ = false;

    PendingJob pending_{};
    bool has_pending_ = false;

    MusicBackgroundImage cache_{};
    MusicBackgroundImage stale_{};
    uint32_t cache_cover_id_ = 0;

    lv_color_t* scratch_ = nullptr;
    uint16_t scratch_w_ = 0;
    uint16_t scratch_h_ = 0;

    ReadyCallback ready_cb_ = nullptr;
    void* ready_user_data_ = nullptr;
};
