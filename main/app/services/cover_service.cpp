#include "cover_service.h"

#include "../core/event/event_bus.h"
#include "app/features/music/util/music_trace.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "extra/libs/sjpg/tjpgd.h"
#include "freertos/task.h"

#include <string.h>

namespace {
[[maybe_unused]] constexpr const char* kTag = "cover_service";
constexpr uint16_t kDecodedCoverSize = CoverService::kCoverSize;
constexpr size_t kJpegWorkBytes = 4096;
constexpr int kDecodeWorkerCore = 1;
constexpr UBaseType_t kDecodeWorkerPriority = 2;
constexpr uint32_t kDecodeWorkerStackBytes = 6 * 1024;

struct JpegDecodeContext {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    uint32_t pos = 0;
    lv_color_t* pixels = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
};

size_t jpegInput(JDEC* jd, uint8_t* buff, size_t ndata)
{
    auto* ctx = static_cast<JpegDecodeContext*>(jd->device);
    if (!ctx || ctx->pos >= ctx->size) {
        return 0;
    }
    const uint32_t left = ctx->size - ctx->pos;
    const uint32_t read = ndata < left ? static_cast<uint32_t>(ndata) : left;
    if (buff) {
        memcpy(buff, ctx->data + ctx->pos, read);
    }
    ctx->pos += read;
    return read;
}

int jpegOutput(JDEC* jd, void* bitmap, JRECT* rect)
{
    auto* ctx = static_cast<JpegDecodeContext*>(jd->device);
    if (!ctx || !ctx->pixels || !bitmap || !rect) {
        return 0;
    }
    const uint8_t* src = static_cast<const uint8_t*>(bitmap);
    for (uint16_t y = rect->top; y <= rect->bottom; ++y) {
        for (uint16_t x = rect->left; x <= rect->right; ++x) {
            const uint8_t r = *src++;
            const uint8_t g = *src++;
            const uint8_t b = *src++;
            if (x < ctx->width && y < ctx->height) {
                ctx->pixels[y * ctx->width + x] = lv_color_make(r, g, b);
            }
        }
    }
    return 1;
}

lv_color_t* allocPixels(uint32_t count)
{
    lv_color_t* p = static_cast<lv_color_t*>(
        heap_caps_malloc(count * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!p) {
        p = static_cast<lv_color_t*>(heap_caps_malloc(count * sizeof(lv_color_t), MALLOC_CAP_8BIT));
    }
    return p;
}

lv_color_t* resampleCoverToSquare(const lv_color_t* src, uint16_t src_w, uint16_t src_h)
{
    if (!src || src_w == 0 || src_h == 0) {
        return nullptr;
    }
    lv_color_t* dst = allocPixels(kDecodedCoverSize * kDecodedCoverSize);
    if (!dst) {
        return nullptr;
    }
    uint16_t crop_x = 0;
    uint16_t crop_y = 0;
    uint16_t crop_w = src_w;
    uint16_t crop_h = src_h;
    if (src_w > src_h) {
        crop_w = src_h;
        crop_x = static_cast<uint16_t>((src_w - crop_w) / 2u);
    } else if (src_h > src_w) {
        crop_h = src_w;
        crop_y = static_cast<uint16_t>((src_h - crop_h) / 2u);
    }
    for (uint16_t y = 0; y < kDecodedCoverSize; ++y) {
        uint16_t sy = static_cast<uint16_t>(crop_y + (static_cast<uint32_t>(y) * crop_h) / kDecodedCoverSize);
        if (sy >= src_h) sy = static_cast<uint16_t>(src_h - 1);
        for (uint16_t x = 0; x < kDecodedCoverSize; ++x) {
            uint16_t sx = static_cast<uint16_t>(crop_x + (static_cast<uint32_t>(x) * crop_w) / kDecodedCoverSize);
            if (sx >= src_w) sx = static_cast<uint16_t>(src_w - 1);
            dst[y * kDecodedCoverSize + x] = src[sy * src_w + sx];
        }
    }
    return dst;
}

// Returns the largest tjpgd scale factor (0–3, meaning 1/1 through 1/8) such
// that both decoded dimensions are still >= kDecodedCoverSize. Falls back to scale 0
// if the image is already smaller than the target — the resample will upsample
// in that case, but there is no better option.
uint8_t pickJpegScale(uint16_t w, uint16_t h)
{
    uint8_t scale = 0;
    while (scale < 3 &&
           (w >> (scale + 1)) >= kDecodedCoverSize &&
           (h >> (scale + 1)) >= kDecodedCoverSize) {
        ++scale;
    }
    return scale;
}

// Decode one JPEG buffer (worker-owned; caller is responsible for freeing
// jpeg_data on return). Does not hold any service mutex.
CoverStatus decodeJpeg(const uint8_t* jpeg_data, uint32_t jpeg_size,
                       lv_color_t** out_pixels)
{
    *out_pixels = nullptr;
    if (!jpeg_data || jpeg_size < 4 ||
        jpeg_data[0] != 0xff || jpeg_data[1] != 0xd8) {
        return CoverStatus::Error;
    }

    uint8_t* work = static_cast<uint8_t*>(heap_caps_malloc(kJpegWorkBytes, MALLOC_CAP_8BIT));
    if (!work) {
        return CoverStatus::Error;
    }

    JpegDecodeContext probe{};
    probe.data = jpeg_data;
    probe.size = jpeg_size;

    JDEC jd{};
    JRESULT rc = jd_prepare(&jd, jpegInput, work, kJpegWorkBytes, &probe);
    if (rc != JDR_OK) {
        ESP_LOGW(kTag, "JPEG prepare failed: %d", static_cast<int>(rc));
        heap_caps_free(work);
        return CoverStatus::Error;
    }

    uint8_t scale = pickJpegScale(jd.width, jd.height);
    uint16_t out_w = static_cast<uint16_t>(jd.width >> scale);
    uint16_t out_h = static_cast<uint16_t>(jd.height >> scale);
    if (out_w == 0) out_w = 1;
    if (out_h == 0) out_h = 1;

    lv_color_t* decoded_pixels = allocPixels(static_cast<uint32_t>(out_w) * out_h);
    if (!decoded_pixels) {
        heap_caps_free(work);
        return CoverStatus::Error;
    }

    JpegDecodeContext decode{};
    decode.data = jpeg_data;
    decode.size = jpeg_size;
    decode.pixels = decoded_pixels;
    decode.width = out_w;
    decode.height = out_h;
    memset(decoded_pixels, 0, static_cast<size_t>(out_w) * out_h * sizeof(lv_color_t));

    rc = jd_prepare(&jd, jpegInput, work, kJpegWorkBytes, &decode);
    if (rc == JDR_OK) {
        rc = jd_decomp(&jd, jpegOutput, scale);
    }
    heap_caps_free(work);

    if (rc != JDR_OK) {
        ESP_LOGW(kTag, "JPEG decode failed: %d", static_cast<int>(rc));
        heap_caps_free(decoded_pixels);
        return CoverStatus::Error;
    }

    lv_color_t* pixels = resampleCoverToSquare(decoded_pixels, out_w, out_h);
    heap_caps_free(decoded_pixels);
    if (!pixels) {
        return CoverStatus::Error;
    }

    ESP_LOGI(kTag, "cover decoded: %ux%u -> %ux%u -> %ux%u",
             jd.width, jd.height, out_w, out_h, kDecodedCoverSize, kDecodedCoverSize);
    *out_pixels = pixels;
    return CoverStatus::Ready;
}
} // namespace

CoverService& CoverService::get()
{
    static CoverService service;
    return service;
}

CoverService::CoverService()
{
    decode_signal_ = xSemaphoreCreateBinary();
}

// acceptJpeg: queue a JPEG for async decode, return cover_id immediately.
// The MQTT task is unblocked in <1 ms; the Core 1 worker decodes without
// competing with the LVGL render task on Core 0.
uint32_t CoverService::acceptJpeg(uint8_t* data, uint32_t size)
{
    if (!data || size == 0) {
        return 0;
    }

    uint32_t cover_id = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        releaseActive();
        cover_id = ++next_cover_id_;
        active_.cover_id = cover_id;
        active_.status = CoverStatus::Loading;
        active_.jpeg_size = size;

        // Transfer JPEG ownership to the pending slot so the worker can
        // read it without holding the mutex. Any previous unprocessed
        // pending is discarded.
        freePendingDecode();
        pending_.jpeg_data = data;
        pending_.jpeg_size = size;
        pending_.cover_id = cover_id;
        has_pending_ = true;
    }

    MUSIC_TRACE_LOGI(kTag, "[trace] cover %u accepted (%u bytes), decode queued",
                     cover_id, static_cast<unsigned>(size));
    publishChanged(cover_id, CoverStatus::Loading);
    ensureDecodeWorkerStarted();
    xSemaphoreGive(decode_signal_);
    return cover_id;
}

CoverState CoverService::active()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stateFromEntry(active_);
}

bool CoverService::borrow(uint32_t cover_id, BorrowedCover* cover)
{
    if (!cover) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (cover_id == 0 || active_.cover_id != cover_id) {
        *cover = BorrowedCover{};
        return false;
    }
    cover->cover_id = active_.cover_id;
    cover->status = active_.status;
    cover->jpeg_data = active_.jpeg_data;
    cover->jpeg_size = active_.jpeg_size;
    cover->image = active_.pixels ? &active_.image : nullptr;
    cover->pixels = active_.pixels;
    return true;
}

bool CoverService::copyPixels(uint32_t cover_id,
                              lv_color_t* dst_pixels,
                              uint32_t dst_pixel_count,
                              lv_img_dsc_t* out_image)
{
    if (cover_id == 0 || !dst_pixels) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (active_.cover_id != cover_id ||
        active_.status != CoverStatus::Ready ||
        !active_.pixels) {
        return false;
    }

    const uint32_t pixel_count = active_.image.header.w * active_.image.header.h;
    if (pixel_count == 0 || pixel_count > dst_pixel_count) {
        return false;
    }

    memcpy(dst_pixels, active_.pixels, pixel_count * sizeof(lv_color_t));
    if (out_image) {
        *out_image = active_.image;
        out_image->data = reinterpret_cast<const uint8_t*>(dst_pixels);
        out_image->data_size = pixel_count * sizeof(lv_color_t);
    }
    return true;
}

void CoverService::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    freePendingDecode();
    releaseActive();
    active_ = CoverEntry{};
}

CoverState CoverService::stateFromEntry(const CoverEntry& entry)
{
    CoverState state{};
    state.cover_id = entry.cover_id;
    state.status = entry.status;
    state.jpeg_size = entry.jpeg_size;
    state.has_jpeg = entry.jpeg_data != nullptr;
    state.has_pixels = entry.pixels != nullptr;
    return state;
}

void CoverService::releaseActive()
{
    if (active_.jpeg_data) {
        heap_caps_free(active_.jpeg_data);
    }
    if (active_.pixels) {
        heap_caps_free(active_.pixels);
    }
    active_ = CoverEntry{};
}

void CoverService::freePendingDecode()
{
    if (has_pending_ && pending_.jpeg_data) {
        heap_caps_free(pending_.jpeg_data);
    }
    pending_ = PendingDecode{};
    has_pending_ = false;
}

void CoverService::ensureDecodeWorkerStarted()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (decode_worker_started_) {
        return;
    }
    decode_worker_started_ = true;
    xTaskCreatePinnedToCore(decodeWorkerTask, "cover_dec",
                            kDecodeWorkerStackBytes, this,
                            kDecodeWorkerPriority, nullptr, kDecodeWorkerCore);
}

void CoverService::decodeWorkerTask(void* arg)
{
    static_cast<CoverService*>(arg)->decodeWorkerLoop();
}

void CoverService::decodeWorkerLoop()
{
    for (;;) {
        xSemaphoreTake(decode_signal_, portMAX_DELAY);
        runOnePendingDecode();
    }
}

bool CoverService::runOnePendingDecode()
{
    PendingDecode job{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!has_pending_) {
            return false;
        }
        job = pending_;
        pending_ = PendingDecode{};
        has_pending_ = false;
    }

#if CLOCK_TRACE_MUSIC
    const uint32_t t_start = static_cast<uint32_t>(xTaskGetTickCount()) * portTICK_PERIOD_MS;
#endif

    // Decode WITHOUT holding the mutex so active() / borrow() are never
    // blocked during the ~250 ms JPEG decode.
    lv_color_t* pixels = nullptr;
    const CoverStatus status = decodeJpeg(job.jpeg_data, job.jpeg_size, &pixels);

#if CLOCK_TRACE_MUSIC
    const uint32_t t_elapsed = (static_cast<uint32_t>(xTaskGetTickCount()) * portTICK_PERIOD_MS) - t_start;
    MUSIC_TRACE_LOGI(kTag, "[trace] cover %u decode done in %u ms, status=%d",
                     job.cover_id, t_elapsed, static_cast<int>(status));
#endif

    // Store result under mutex only if this cover_id is still current.
    bool stored = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_.cover_id == job.cover_id) {
            if (active_.pixels) {
                heap_caps_free(active_.pixels);
            }
            active_.pixels = pixels;
            active_.jpeg_data = job.jpeg_data;  // transfer ownership back
            active_.status = status;
            if (status == CoverStatus::Ready && pixels) {
                active_.image = {};
                active_.image.header.always_zero = 0;
                active_.image.header.w = kDecodedCoverSize;
                active_.image.header.h = kDecodedCoverSize;
                active_.image.header.cf = LV_IMG_CF_TRUE_COLOR;
                active_.image.data = reinterpret_cast<const uint8_t*>(pixels);
                active_.image.data_size = kDecodedCoverSize * kDecodedCoverSize * sizeof(lv_color_t);
            }
            stored = true;
        }
    }

    if (!stored) {
        // This cover was superseded by a newer one; discard our results.
        if (pixels) {
            heap_caps_free(pixels);
        }
        heap_caps_free(job.jpeg_data);
        return true;
    }

    // job.jpeg_data ownership was transferred back into active_.jpeg_data above.
    publishChanged(job.cover_id, status);
    return true;
}

void CoverService::publishChanged(uint32_t cover_id, CoverStatus status)
{
    AppEvent event{};
    event.type = AppEventType::CoverStateChanged;
    event.payload.cover_state.cover_id = cover_id;
    event.payload.cover_state.status = status;
    EventBus::get().publish(event);
}
