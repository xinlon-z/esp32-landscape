#include "background_blur_service.h"

#include "app/features/music/util/music_background.h"
#include "app/services/cover_service.h"
#include "platform/lvgl_port.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/task.h"

#include <string.h>

namespace {
constexpr const char* kTag = "bg_blur_svc";
constexpr int kWorkerCore = 1;
constexpr UBaseType_t kWorkerPriority = 1;
constexpr uint32_t kWorkerStackBytes = 6 * 1024;
constexpr uint32_t kStaleFreeDelayMs = 50;
constexpr uint32_t kScratchRetentionMs = 3000;

lv_color_t* allocPixels(size_t count)
{
    return static_cast<lv_color_t*>(heap_caps_malloc(count * sizeof(lv_color_t),
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

void buildImageHeader(MusicBackgroundImage* image, uint16_t w, uint16_t h)
{
    image->image.header.always_zero = 0;
    image->image.header.w = w;
    image->image.header.h = h;
    image->image.header.cf = LV_IMG_CF_TRUE_COLOR;
    image->image.data = reinterpret_cast<const uint8_t*>(image->pixels);
    image->image.data_size = static_cast<uint32_t>(w) * h * sizeof(lv_color_t);
}
} // namespace

BackgroundBlurService& BackgroundBlurService::get()
{
    static BackgroundBlurService service;
    return service;
}

BackgroundBlurService::BackgroundBlurService()
{
    mutex_ = xSemaphoreCreateMutex();
    signal_ = xSemaphoreCreateBinary();
}

bool BackgroundBlurService::tryGetCached(uint32_t cover_id, const lv_img_dsc_t** out_image)
{
    if (cover_id == 0) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool hit = cache_cover_id_ == cover_id && cache_.pixels != nullptr;
    if (hit && out_image) {
        *out_image = &cache_.image;
    }
    xSemaphoreGive(mutex_);
    return hit;
}

bool BackgroundBlurService::request(const BorrowedCover& cover,
                                    uint16_t target_w,
                                    uint16_t target_h,
                                    const lv_img_dsc_t** out_image)
{
    if (cover.cover_id == 0 || !cover.pixels || !cover.image || target_w == 0 || target_h == 0) {
        return false;
    }

    const uint16_t src_w = static_cast<uint16_t>(cover.image->header.w);
    const uint16_t src_h = static_cast<uint16_t>(cover.image->header.h);
    if (src_w == 0 || src_h == 0) {
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (cache_cover_id_ == cover.cover_id && cache_.pixels) {
        if (out_image) {
            *out_image = &cache_.image;
        }
        xSemaphoreGive(mutex_);
        return true;
    }

    if (has_pending_ && pending_.cover_id == cover.cover_id) {
        xSemaphoreGive(mutex_);
        ensureWorkerStarted();
        return false;
    }

    if (has_pending_) {
        freeJob(&pending_);
        has_pending_ = false;
    }
    xSemaphoreGive(mutex_);

    lv_color_t* src_copy = allocPixels(static_cast<size_t>(src_w) * src_h);
    if (!src_copy) {
        ESP_LOGW(kTag, "failed to alloc src copy %ux%u", src_w, src_h);
        return false;
    }
    memcpy(src_copy, cover.pixels, static_cast<size_t>(src_w) * src_h * sizeof(lv_color_t));

    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.cover_id = cover.cover_id;
    pending_.src_pixels = src_copy;
    pending_.src_w = src_w;
    pending_.src_h = src_h;
    pending_.target_w = target_w;
    pending_.target_h = target_h;
    has_pending_ = true;
    xSemaphoreGive(mutex_);

    ensureWorkerStarted();
    xSemaphoreGive(signal_);
    return false;
}

void BackgroundBlurService::setReadyCallback(ReadyCallback callback, void* user_data)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    ready_cb_ = callback;
    ready_user_data_ = user_data;
    xSemaphoreGive(mutex_);
}

void BackgroundBlurService::clearReadyCallback(void* user_data)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (ready_user_data_ == user_data) {
        ready_cb_ = nullptr;
        ready_user_data_ = nullptr;
    }
    xSemaphoreGive(mutex_);
}

void BackgroundBlurService::ensureWorkerStarted()
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool need_start = !worker_started_;
    worker_started_ = true;
    xSemaphoreGive(mutex_);
    if (need_start) {
        xTaskCreatePinnedToCore(taskTrampoline, "bg_blur",
                                kWorkerStackBytes, this,
                                kWorkerPriority, nullptr, kWorkerCore);
    }
}

void BackgroundBlurService::taskTrampoline(void* arg)
{
    static_cast<BackgroundBlurService*>(arg)->taskLoop();
}

void BackgroundBlurService::taskLoop()
{
    for (;;) {
        const TickType_t wait = scratch_ ? pdMS_TO_TICKS(kScratchRetentionMs) : portMAX_DELAY;
        if (xSemaphoreTake(signal_, wait) != pdTRUE) {
            // Idle past the retention window — release the scratch buffer.
            if (scratch_) {
                heap_caps_free(scratch_);
                scratch_ = nullptr;
                scratch_w_ = 0;
                scratch_h_ = 0;
            }
            continue;
        }

        for (;;) {
            PendingJob job{};
            xSemaphoreTake(mutex_, portMAX_DELAY);
            const bool have = has_pending_;
            if (have) {
                job = pending_;
                pending_ = PendingJob{};
                has_pending_ = false;
            }
            xSemaphoreGive(mutex_);
            if (!have) {
                break;
            }

            if (!scratch_ || scratch_w_ != job.target_w || scratch_h_ != job.target_h) {
                if (scratch_) {
                    heap_caps_free(scratch_);
                    scratch_ = nullptr;
                }
                scratch_ = allocPixels(static_cast<size_t>(job.target_w) * job.target_h);
                scratch_w_ = job.target_w;
                scratch_h_ = job.target_h;
                if (!scratch_) {
                    ESP_LOGW(kTag, "failed to alloc scratch %ux%u", job.target_w, job.target_h);
                    freeJob(&job);
                    continue;
                }
            }

            lv_color_t* out_pixels = allocPixels(static_cast<size_t>(job.target_w) * job.target_h);
            if (!out_pixels) {
                ESP_LOGW(kTag, "failed to alloc output %ux%u", job.target_w, job.target_h);
                freeJob(&job);
                continue;
            }

            const uint32_t t_blur_start = static_cast<uint32_t>(xTaskGetTickCount()) * portTICK_PERIOD_MS;
            const bool ok = musicGenerateBlurredBackground(
                job.src_pixels, job.src_w, job.src_h,
                out_pixels, job.target_w, job.target_h, scratch_);
            const uint32_t t_blur_elapsed =
                (static_cast<uint32_t>(xTaskGetTickCount()) * portTICK_PERIOD_MS) - t_blur_start;
            ESP_LOGI(kTag, "[trace] blur cover=%u took %u ms ok=%d",
                     job.cover_id, t_blur_elapsed, ok ? 1 : 0);
            if (!ok) {
                heap_caps_free(out_pixels);
                freeJob(&job);
                continue;
            }

            MusicBackgroundImage next{};
            next.pixels = out_pixels;
            buildImageHeader(&next, job.target_w, job.target_h);

            // The cache swap publishes new cache_.image.data to the LVGL
            // render task (Core 0). Hold LvglPort here to make the swap
            // atomic relative to lv_timer_handler — otherwise the renderer
            // can read a half-updated descriptor mid-blit and produce torn
            // (striped) output, especially noticeable on the music UI's
            // background image at 30 FPS visualizer rates. Lock order is
            // LvglPort -> mutex_, matching the LVGL-thread path.
            LvglPort::lock();

            xSemaphoreTake(mutex_, portMAX_DELAY);
            const bool superseded = has_pending_ && pending_.cover_id != job.cover_id;
            if (superseded) {
                xSemaphoreGive(mutex_);
                LvglPort::unlock();
                heap_caps_free(out_pixels);
                freeJob(&job);
                continue;
            }
            musicReleaseBackgroundImage(&stale_);
            stale_ = cache_;
            cache_ = next;
            cache_cover_id_ = job.cover_id;
            xSemaphoreGive(mutex_);

            heap_caps_free(job.src_pixels);
            job.src_pixels = nullptr;

            lv_async_call(onAsyncReady, this);
            LvglPort::unlock();
        }
    }
}

void BackgroundBlurService::onAsyncReady(void* user_data)
{
    auto* svc = static_cast<BackgroundBlurService*>(user_data);
    uint32_t cover_id = 0;
    ReadyCallback cb = nullptr;
    void* ud = nullptr;
    bool need_free = false;
    xSemaphoreTake(svc->mutex_, portMAX_DELAY);
    cover_id = svc->cache_cover_id_;
    cb = svc->ready_cb_;
    ud = svc->ready_user_data_;
    need_free = svc->stale_.pixels != nullptr;
    xSemaphoreGive(svc->mutex_);
    ESP_LOGI(kTag, "[trace] async ready cover=%u stale_alive=%d", cover_id, need_free ? 1 : 0);
    if (cb) {
        cb(cover_id, ud);
    }

    // Defer stale_ release one render tick beyond now. lv_img_set_src has already
    // bound the new image, but we let LVGL complete at least one redraw cycle
    // before freeing the prior pixels — a belt-and-braces guard against any
    // in-flight reference to stale_.pixels.
    if (need_free) {
        lv_timer_t* timer = lv_timer_create(onStaleTimer, kStaleFreeDelayMs, svc);
        if (timer) {
            lv_timer_set_repeat_count(timer, 1);
        } else {
            xSemaphoreTake(svc->mutex_, portMAX_DELAY);
            if (svc->stale_.pixels) {
                lv_img_cache_invalidate_src(&svc->stale_.image);
                musicReleaseBackgroundImage(&svc->stale_);
            }
            xSemaphoreGive(svc->mutex_);
        }
    }
}

void BackgroundBlurService::onStaleTimer(lv_timer_t* timer)
{
    auto* svc = static_cast<BackgroundBlurService*>(timer->user_data);
    xSemaphoreTake(svc->mutex_, portMAX_DELAY);
    if (svc->stale_.pixels) {
        lv_img_cache_invalidate_src(&svc->stale_.image);
        musicReleaseBackgroundImage(&svc->stale_);
    }
    xSemaphoreGive(svc->mutex_);
}

void BackgroundBlurService::freeJob(PendingJob* job)
{
    if (!job) {
        return;
    }
    if (job->src_pixels) {
        heap_caps_free(job->src_pixels);
    }
    *job = PendingJob{};
}
