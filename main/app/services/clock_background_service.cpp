#include "clock_background_service.h"

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "extra/libs/sjpg/tjpgd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <mutex>
#include <stdio.h>
#include <string.h>
#include <strings.h>

namespace {
constexpr const char* kTag = "clock_bg";
constexpr size_t kJpegWorkBytes = 4096;
constexpr uint32_t kMaxJpegBytes = 512 * 1024;
constexpr int kMaxRedirects = 3;
constexpr UBaseType_t kDownloadTaskPriority = 2;
constexpr uint32_t kDownloadTaskStackBytes = 7 * 1024;
constexpr int kDownloadTaskCore = 1;

std::mutex& serviceMutex()
{
    static std::mutex mutex;
    return mutex;
}

struct ClockBackgroundJpegContext {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    uint32_t pos = 0;
    lv_color_t* pixels = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
};

struct DecodedClockBackground {
    lv_img_dsc_t image{};
    lv_color_t* pixels = nullptr;
};

struct HttpHeaderContext {
    ClockForegroundPalette palette{};
};

enum class ClockBackgroundDecodeStatus : uint8_t {
    Ready,
    Error,
    SizeMismatch,
};

size_t clockBackgroundJpegInput(JDEC* jd, uint8_t* buff, size_t ndata)
{
    auto* ctx = static_cast<ClockBackgroundJpegContext*>(jd->device);
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

int clockBackgroundJpegOutput(JDEC* jd, void* bitmap, JRECT* rect)
{
    auto* ctx = static_cast<ClockBackgroundJpegContext*>(jd->device);
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

uint32_t nowMs()
{
    return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

uint32_t normalizeRefreshInterval(uint32_t interval_ms)
{
    return interval_ms > 0 ? interval_ms : ClockBackgroundService::kDefaultRefreshIntervalMs;
}

void copyBackgroundUrl(char* dst, size_t dst_size, const char* src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    const char* value = (src && src[0]) ? src : CLOCK_BACKGROUND_URL;
    snprintf(dst, dst_size, "%s", value);
    dst[dst_size - 1] = '\0';
}

lv_color_t* allocClockBackgroundPixels()
{
    lv_color_t* pixels = static_cast<lv_color_t*>(
        heap_caps_malloc(ClockBackgroundService::kPixelCount * sizeof(lv_color_t),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!pixels) {
        pixels = static_cast<lv_color_t*>(
            heap_caps_malloc(ClockBackgroundService::kPixelCount * sizeof(lv_color_t),
                             MALLOC_CAP_8BIT));
    }
    return pixels;
}

bool isRedirectStatus(int status_code)
{
    return status_code == 301 ||
           status_code == 302 ||
           status_code == 303 ||
           status_code == 307 ||
           status_code == 308;
}

int hexValue(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool parseHexColor(const char* value, uint32_t* out)
{
    if (!value || !out) {
        return false;
    }
    if (value[0] == '#') {
        ++value;
    }

    uint32_t color = 0;
    for (int i = 0; i < 6; ++i) {
        const int v = hexValue(value[i]);
        if (v < 0) {
            return false;
        }
        color = (color << 4) | static_cast<uint32_t>(v);
    }
    if (value[6] != '\0') {
        return false;
    }

    *out = color;
    return true;
}

void applyPaletteHeader(ClockForegroundPalette* palette,
                        const char* key,
                        const char* value)
{
    if (!palette || !key || !value) {
        return;
    }

    uint32_t color = 0;
    if (!parseHexColor(value, &color)) {
        return;
    }

    if (strcasecmp(key, "X-Clock-Fg") == 0) {
        palette->fg = color;
    } else if (strcasecmp(key, "X-Clock-Dim") == 0) {
        palette->dim = color;
    } else if (strcasecmp(key, "X-Clock-Muted") == 0) {
        palette->muted = color;
    } else if (strcasecmp(key, "X-Clock-Faint") == 0) {
        palette->faint = color;
    } else if (strcasecmp(key, "X-Clock-Accent") == 0) {
        palette->accent = color;
    }
}

esp_err_t onHttpEvent(esp_http_client_event_t* event)
{
    if (!event || event->event_id != HTTP_EVENT_ON_HEADER) {
        return ESP_OK;
    }

    auto* ctx = static_cast<HttpHeaderContext*>(event->user_data);
    if (!ctx) {
        return ESP_OK;
    }

    applyPaletteHeader(&ctx->palette, event->header_key, event->header_value);
    return ESP_OK;
}

ClockBackgroundDecodeStatus decodeClockBackgroundJpeg(const uint8_t* jpeg_data,
                                                      uint32_t jpeg_size,
                                                      DecodedClockBackground* out)
{
    if (out) {
        *out = DecodedClockBackground{};
    }
    if (!out || !jpeg_data || jpeg_size < 4 ||
        jpeg_data[0] != 0xff || jpeg_data[1] != 0xd8) {
        return ClockBackgroundDecodeStatus::Error;
    }

    uint8_t* work = static_cast<uint8_t*>(heap_caps_malloc(kJpegWorkBytes, MALLOC_CAP_8BIT));
    if (!work) {
        return ClockBackgroundDecodeStatus::Error;
    }

    ClockBackgroundJpegContext probe{};
    probe.data = jpeg_data;
    probe.size = jpeg_size;

    JDEC jd{};
    JRESULT rc = jd_prepare(&jd, clockBackgroundJpegInput, work, kJpegWorkBytes, &probe);
    if (rc != JDR_OK) {
        ESP_LOGW(kTag, "JPEG prepare failed: %d", static_cast<int>(rc));
        heap_caps_free(work);
        return ClockBackgroundDecodeStatus::Error;
    }

    if (jd.width != ClockBackgroundService::kWidth ||
        jd.height != ClockBackgroundService::kHeight) {
        ESP_LOGW(kTag, "discarding background %ux%u, expected %ux%u",
                 jd.width, jd.height,
                 ClockBackgroundService::kWidth, ClockBackgroundService::kHeight);
        heap_caps_free(work);
        return ClockBackgroundDecodeStatus::SizeMismatch;
    }

    lv_color_t* pixels = allocClockBackgroundPixels();
    if (!pixels) {
        heap_caps_free(work);
        return ClockBackgroundDecodeStatus::Error;
    }
    memset(pixels, 0, ClockBackgroundService::kPixelCount * sizeof(lv_color_t));

    ClockBackgroundJpegContext decode{};
    decode.data = jpeg_data;
    decode.size = jpeg_size;
    decode.pixels = pixels;
    decode.width = ClockBackgroundService::kWidth;
    decode.height = ClockBackgroundService::kHeight;

    rc = jd_prepare(&jd, clockBackgroundJpegInput, work, kJpegWorkBytes, &decode);
    if (rc == JDR_OK) {
        rc = jd_decomp(&jd, clockBackgroundJpegOutput, 0);
    }
    heap_caps_free(work);

    if (rc != JDR_OK) {
        ESP_LOGW(kTag, "JPEG decode failed: %d", static_cast<int>(rc));
        heap_caps_free(pixels);
        return ClockBackgroundDecodeStatus::Error;
    }

    out->pixels = pixels;
    out->image.header.always_zero = 0;
    out->image.header.w = ClockBackgroundService::kWidth;
    out->image.header.h = ClockBackgroundService::kHeight;
    out->image.header.cf = LV_IMG_CF_TRUE_COLOR;
    out->image.data = reinterpret_cast<const uint8_t*>(pixels);
    out->image.data_size = ClockBackgroundService::kPixelCount * sizeof(lv_color_t);
    return ClockBackgroundDecodeStatus::Ready;
}

uint8_t* downloadBackgroundJpeg(const char* url,
                                uint32_t* out_size,
                                ClockForegroundPalette* out_palette = nullptr)
{
    if (out_size) {
        *out_size = 0;
    }

    esp_http_client_config_t config{};
    HttpHeaderContext header_ctx{};
    config.url = (url && url[0]) ? url : CLOCK_BACKGROUND_URL;
    config.timeout_ms = 8000;
    config.max_redirection_count = kMaxRedirects;
    config.event_handler = onHttpEvent;
    config.user_data = &header_ctx;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return nullptr;
    }

    uint8_t* data = nullptr;
    uint32_t total = 0;
    int redirects = 0;
    while (redirects <= kMaxRedirects) {
        if (esp_http_client_open(client, 0) != ESP_OK) {
            ESP_LOGW(kTag, "HTTP open failed");
            break;
        }

        const int64_t content_length = esp_http_client_fetch_headers(client);
        const int status_code = esp_http_client_get_status_code(client);
        if (isRedirectStatus(status_code)) {
            if (redirects >= kMaxRedirects) {
                ESP_LOGW(kTag, "HTTP redirect limit reached at status %d", status_code);
                break;
            }
            if (esp_http_client_set_redirection(client) != ESP_OK) {
                ESP_LOGW(kTag, "HTTP redirect setup failed for status %d", status_code);
                break;
            }
            ++redirects;
            esp_http_client_close(client);
            continue;
        }
        if (status_code != 200) {
            ESP_LOGW(kTag, "HTTP status %d", status_code);
            break;
        }

        if (content_length > static_cast<int64_t>(kMaxJpegBytes)) {
            ESP_LOGW(kTag, "background JPEG too large: %lld bytes",
                     static_cast<long long>(content_length));
            break;
        }

        const uint32_t capacity = content_length > 0 ?
            static_cast<uint32_t>(content_length) : kMaxJpegBytes;
        data = static_cast<uint8_t*>(
            heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!data) {
            data = static_cast<uint8_t*>(heap_caps_malloc(capacity, MALLOC_CAP_8BIT));
        }
        if (!data) {
            break;
        }

        while (total < capacity) {
            const int read = esp_http_client_read(client,
                                                  reinterpret_cast<char*>(data + total),
                                                  static_cast<int>(capacity - total));
            if (read < 0) {
                ESP_LOGW(kTag, "HTTP read failed");
                heap_caps_free(data);
                data = nullptr;
                total = 0;
                break;
            }
            if (read == 0) {
                break;
            }
            total += static_cast<uint32_t>(read);
        }

        if (!data) {
            break;
        }
        if (content_length > 0 && total != static_cast<uint32_t>(content_length)) {
            ESP_LOGW(kTag, "short background download: %u/%lld",
                     static_cast<unsigned>(total),
                     static_cast<long long>(content_length));
            heap_caps_free(data);
            data = nullptr;
            total = 0;
            break;
        }
        break;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (data && total > 0) {
        if (out_size) {
            *out_size = total;
        }
        if (out_palette) {
            *out_palette = header_ctx.palette;
        }
    } else if (data) {
        heap_caps_free(data);
        data = nullptr;
    }
    return data;
}
} // namespace

ClockBackgroundService& ClockBackgroundService::get()
{
    static ClockBackgroundService service;
    return service;
}

ClockBackgroundService::ClockBackgroundService()
{
    copyBackgroundUrl(url_, sizeof(url_), CLOCK_BACKGROUND_URL);
    refresh_interval_ms_ = normalizeRefreshInterval(
        static_cast<uint32_t>(CLOCK_BACKGROUND_REFRESH_INTERVAL_MS));
}

void ClockBackgroundService::configure(const ClockBackgroundConfig& config)
{
    char next_url[kMaxUrlLength]{};
    copyBackgroundUrl(next_url, sizeof(next_url), config.url);
    const uint32_t next_interval = normalizeRefreshInterval(config.refresh_interval_ms);

    std::lock_guard<std::mutex> lock(serviceMutex());
    const bool changed = strcmp(url_, next_url) != 0 ||
                         refresh_interval_ms_ != next_interval;
    memcpy(url_, next_url, sizeof(url_));
    refresh_interval_ms_ = next_interval;
    if (changed) {
        refresh_due_immediately_ = true;
    }
}

ClockBackgroundConfig ClockBackgroundService::configSnapshot()
{
    std::lock_guard<std::mutex> lock(serviceMutex());
    return {url_, refresh_interval_ms_};
}

void ClockBackgroundService::requestRefresh()
{
    startRefresh(nowMs());
}

bool ClockBackgroundService::requestRefreshIfDue()
{
    return requestRefreshIfDue(nowMs());
}

bool ClockBackgroundService::requestRefreshIfDue(uint32_t now_ms)
{
    {
        std::lock_guard<std::mutex> lock(serviceMutex());
        if (loading_) {
            return false;
        }
        const uint32_t elapsed_ms = now_ms - last_request_ms_;
        if (!refresh_due_immediately_ && elapsed_ms < refresh_interval_ms_) {
            return false;
        }
    }

    return startRefresh(now_ms);
}

bool ClockBackgroundService::startRefresh(uint32_t now_ms)
{
    {
        std::lock_guard<std::mutex> lock(serviceMutex());
        if (loading_) {
            return false;
        }
        loading_ = true;
        status_ = ClockBackgroundStatus::Loading;
        last_request_ms_ = now_ms;
        refresh_due_immediately_ = false;
    }

    const BaseType_t created =
        xTaskCreatePinnedToCore(downloadTask, "clock_bg",
                                kDownloadTaskStackBytes, this,
                                kDownloadTaskPriority, nullptr, kDownloadTaskCore);
    if (created != pdPASS) {
        std::lock_guard<std::mutex> lock(serviceMutex());
        loading_ = false;
        status_ = ClockBackgroundStatus::Error;
        ++revision_;
        return false;
    }
    return true;
}

ClockBackgroundState ClockBackgroundService::snapshot()
{
    std::lock_guard<std::mutex> lock(serviceMutex());
    ClockBackgroundState state{};
    state.revision = revision_;
    state.status = status_;
    state.jpeg_size = jpeg_size_;
    state.has_pixels = pixels_ != nullptr;
    state.palette = palette_;
    return state;
}

bool ClockBackgroundService::copyPixels(lv_color_t* dst_pixels,
                                        uint32_t dst_pixel_count,
                                        lv_img_dsc_t* out_image,
                                        uint32_t* out_revision)
{
    if (out_revision) {
        *out_revision = 0;
    }
    if (!dst_pixels || dst_pixel_count < kPixelCount) {
        return false;
    }

    std::lock_guard<std::mutex> lock(serviceMutex());
    if (!pixels_ || status_ != ClockBackgroundStatus::Ready) {
        return false;
    }

    memcpy(dst_pixels, pixels_, kPixelCount * sizeof(lv_color_t));
    if (out_image) {
        out_image->header.always_zero = 0;
        out_image->header.w = kWidth;
        out_image->header.h = kHeight;
        out_image->header.cf = LV_IMG_CF_TRUE_COLOR;
        out_image->data = reinterpret_cast<const uint8_t*>(dst_pixels);
        out_image->data_size = kPixelCount * sizeof(lv_color_t);
    }
    if (out_revision) {
        *out_revision = revision_;
    }
    return true;
}

void ClockBackgroundService::clear()
{
    std::lock_guard<std::mutex> lock(serviceMutex());
    releasePixels();
    revision_ = 0;
    status_ = ClockBackgroundStatus::Idle;
    jpeg_size_ = 0;
    last_request_ms_ = 0;
    loading_ = false;
    refresh_due_immediately_ = true;
}

void ClockBackgroundService::downloadTask(void* arg)
{
    auto* self = static_cast<ClockBackgroundService*>(arg);
    if (self) {
        self->runDownload();
    }
    vTaskDelete(nullptr);
}

void ClockBackgroundService::runDownload()
{
    char url[kMaxUrlLength]{};
    {
        std::lock_guard<std::mutex> lock(serviceMutex());
        memcpy(url, url_, sizeof(url));
        url[sizeof(url) - 1] = '\0';
    }

    uint32_t jpeg_size = 0;
    ClockForegroundPalette palette{};
    uint8_t* jpeg_data = downloadBackgroundJpeg(url, &jpeg_size, &palette);
    if (!jpeg_data) {
        std::lock_guard<std::mutex> lock(serviceMutex());
        status_ = ClockBackgroundStatus::Error;
        jpeg_size_ = 0;
        loading_ = false;
        ++revision_;
        return;
    }

    acceptJpeg(jpeg_data, jpeg_size, palette);
    heap_caps_free(jpeg_data);
}

bool ClockBackgroundService::acceptJpeg(uint8_t* jpeg_data,
                                        uint32_t jpeg_size,
                                        const ClockForegroundPalette& palette)
{
    DecodedClockBackground decoded{};
    const ClockBackgroundDecodeStatus decode_status =
        decodeClockBackgroundJpeg(jpeg_data, jpeg_size, &decoded);

    std::lock_guard<std::mutex> lock(serviceMutex());
    loading_ = false;
    jpeg_size_ = jpeg_size;
    ++revision_;

    if (decode_status == ClockBackgroundDecodeStatus::Ready && decoded.pixels) {
        releasePixels();
        pixels_ = decoded.pixels;
        palette_ = palette;
        status_ = ClockBackgroundStatus::Ready;
        ESP_LOGI(kTag, "background ready: %ux%u, %u bytes",
                 kWidth, kHeight, static_cast<unsigned>(jpeg_size));
        return true;
    }

    if (decoded.pixels) {
        heap_caps_free(decoded.pixels);
    }
    status_ = decode_status == ClockBackgroundDecodeStatus::SizeMismatch ?
        ClockBackgroundStatus::SizeMismatch : ClockBackgroundStatus::Error;
    return false;
}

void ClockBackgroundService::releasePixels()
{
    if (pixels_) {
        heap_caps_free(pixels_);
        pixels_ = nullptr;
    }
}
