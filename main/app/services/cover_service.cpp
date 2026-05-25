#include "cover_service.h"

#include "../core/event/event_bus.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "extra/libs/sjpg/tjpgd.h"

#include <string.h>

namespace {
[[maybe_unused]] constexpr const char* kTag = "cover_service";
constexpr uint16_t kCoverSize = 144;
constexpr size_t kJpegWorkBytes = 4096;

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
    lv_color_t* pixels = static_cast<lv_color_t*>(
        heap_caps_malloc(count * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!pixels) {
        pixels = static_cast<lv_color_t*>(heap_caps_malloc(count * sizeof(lv_color_t), MALLOC_CAP_8BIT));
    }
    return pixels;
}

lv_color_t* resampleCoverToSquare(const lv_color_t* src, uint16_t src_w, uint16_t src_h)
{
    if (!src || src_w == 0 || src_h == 0) {
        return nullptr;
    }

    lv_color_t* dst = allocPixels(kCoverSize * kCoverSize);
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

    for (uint16_t y = 0; y < kCoverSize; ++y) {
        uint16_t sy = static_cast<uint16_t>(crop_y + (static_cast<uint32_t>(y) * crop_h) / kCoverSize);
        if (sy >= src_h) {
            sy = static_cast<uint16_t>(src_h - 1);
        }
        for (uint16_t x = 0; x < kCoverSize; ++x) {
            uint16_t sx = static_cast<uint16_t>(crop_x + (static_cast<uint32_t>(x) * crop_w) / kCoverSize);
            if (sx >= src_w) {
                sx = static_cast<uint16_t>(src_w - 1);
            }
            dst[y * kCoverSize + x] = src[sy * src_w + sx];
        }
    }

    return dst;
}
} // namespace

CoverService& CoverService::get()
{
    static CoverService service;
    return service;
}

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
        active_.jpeg_data = data;
        active_.jpeg_size = size;
    }

    publishChanged(cover_id, CoverStatus::Loading);
    const CoverStatus decoded_status = decodeActiveJpeg(cover_id);
    publishChanged(cover_id, decoded_status);
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

void CoverService::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
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

CoverStatus CoverService::decodeActiveJpeg(uint32_t cover_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (cover_id == 0 || active_.cover_id != cover_id || !active_.jpeg_data || active_.jpeg_size < 4 ||
        active_.jpeg_data[0] != 0xff || active_.jpeg_data[1] != 0xd8) {
        if (active_.cover_id == cover_id) {
            active_.status = CoverStatus::Error;
        }
        return CoverStatus::Error;
    }

    uint8_t* work = static_cast<uint8_t*>(heap_caps_malloc(kJpegWorkBytes, MALLOC_CAP_8BIT));
    if (!work) {
        active_.status = CoverStatus::Error;
        return CoverStatus::Error;
    }

    JpegDecodeContext probe{};
    probe.data = active_.jpeg_data;
    probe.size = active_.jpeg_size;

    JDEC jd{};
    JRESULT rc = jd_prepare(&jd, jpegInput, work, kJpegWorkBytes, &probe);
    if (rc != JDR_OK) {
        ESP_LOGW(kTag, "JPEG prepare failed: %d", static_cast<int>(rc));
        heap_caps_free(work);
        active_.status = CoverStatus::Error;
        return CoverStatus::Error;
    }

    uint8_t scale = 0;
    while (scale < 3 && ((jd.width >> scale) > kCoverSize || (jd.height >> scale) > kCoverSize)) {
        ++scale;
    }
    uint16_t out_w = static_cast<uint16_t>(jd.width >> scale);
    uint16_t out_h = static_cast<uint16_t>(jd.height >> scale);
    if (out_w == 0) {
        out_w = 1;
    }
    if (out_h == 0) {
        out_h = 1;
    }

    lv_color_t* decoded_pixels = allocPixels(static_cast<uint32_t>(out_w) * out_h);
    if (!decoded_pixels) {
        heap_caps_free(work);
        active_.status = CoverStatus::Error;
        return CoverStatus::Error;
    }

    JpegDecodeContext decode{};
    decode.data = active_.jpeg_data;
    decode.size = active_.jpeg_size;
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
        active_.status = CoverStatus::Error;
        return CoverStatus::Error;
    }

    lv_color_t* pixels = resampleCoverToSquare(decoded_pixels, out_w, out_h);
    heap_caps_free(decoded_pixels);
    if (!pixels) {
        active_.status = CoverStatus::Error;
        return CoverStatus::Error;
    }

    if (active_.pixels) {
        heap_caps_free(active_.pixels);
    }
    active_.pixels = pixels;
    active_.image = {};
    active_.image.header.always_zero = 0;
    active_.image.header.w = kCoverSize;
    active_.image.header.h = kCoverSize;
    active_.image.header.cf = LV_IMG_CF_TRUE_COLOR;
    active_.image.data = reinterpret_cast<const uint8_t*>(active_.pixels);
    active_.image.data_size = kCoverSize * kCoverSize * sizeof(lv_color_t);
    active_.status = CoverStatus::Ready;
    ESP_LOGI(kTag, "cover decoded: %ux%u -> %ux%u -> %ux%u",
             jd.width, jd.height, out_w, out_h, kCoverSize, kCoverSize);
    return CoverStatus::Ready;
}

void CoverService::publishChanged(uint32_t cover_id, CoverStatus status)
{
    AppEvent event{};
    event.type = AppEventType::CoverStateChanged;
    event.payload.cover_state.cover_id = cover_id;
    event.payload.cover_state.status = status;
    EventBus::get().publish(event);
}
