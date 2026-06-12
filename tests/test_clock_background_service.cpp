#include <gtest/gtest.h>

#define private public
#define protected public
#include "app/services/clock_background_service.cpp"
#undef private
#undef protected

#include "esp_heap_caps.h"
#include "extra/libs/sjpg/tjpgd.h"

#include <string.h>

namespace {
uint8_t kMinimalJpeg[] = {0xff, 0xd8, 0xff, 0xd9};
} // namespace

TEST(ClockBackgroundDecode, RejectsDecodedImageWhenResolutionDoesNotMatchScreen)
{
    tjpgdStubSetDimensions(320, 172);

    DecodedClockBackground decoded{};
    const ClockBackgroundDecodeStatus status =
        decodeClockBackgroundJpeg(kMinimalJpeg, sizeof(kMinimalJpeg), &decoded);

    EXPECT_EQ(status, ClockBackgroundDecodeStatus::SizeMismatch);
    EXPECT_EQ(decoded.pixels, nullptr);
    EXPECT_EQ(decoded.image.data, nullptr);
}

TEST(ClockBackgroundDecode, AcceptsExactScreenResolution)
{
    tjpgdStubSetDimensions(ClockBackgroundService::kWidth,
                           ClockBackgroundService::kHeight);

    DecodedClockBackground decoded{};
    const ClockBackgroundDecodeStatus status =
        decodeClockBackgroundJpeg(kMinimalJpeg, sizeof(kMinimalJpeg), &decoded);

    ASSERT_EQ(status, ClockBackgroundDecodeStatus::Ready);
    ASSERT_NE(decoded.pixels, nullptr);
    EXPECT_EQ(decoded.image.header.w, ClockBackgroundService::kWidth);
    EXPECT_EQ(decoded.image.header.h, ClockBackgroundService::kHeight);
    EXPECT_EQ(decoded.image.header.cf, LV_IMG_CF_TRUE_COLOR);
    EXPECT_EQ(decoded.image.data, reinterpret_cast<const uint8_t*>(decoded.pixels));
    EXPECT_EQ(decoded.image.data_size,
              ClockBackgroundService::kPixelCount * sizeof(lv_color_t));

    heap_caps_free(decoded.pixels);
}

TEST(ClockBackgroundDownload, FollowsRedirectBeforeReadingImage)
{
    auto& http = espHttpClientStubState();
    http.reset();
    http.responses[0].status_code = 302;
    http.responses[0].content_length = 0;
    http.responses[1].status_code = 200;
    http.responses[1].content_length = sizeof(kMinimalJpeg);
    http.responses[1].body = kMinimalJpeg;
    http.responses[1].body_size = sizeof(kMinimalJpeg);

    uint32_t size = 0;
    uint8_t* jpeg = downloadBackgroundJpeg("http://example.test/random-clock", &size);

    ASSERT_NE(jpeg, nullptr);
    EXPECT_EQ(size, sizeof(kMinimalJpeg));
    EXPECT_EQ(memcmp(jpeg, kMinimalJpeg, sizeof(kMinimalJpeg)), 0);
    EXPECT_STREQ(http.init_url, "http://example.test/random-clock");
    EXPECT_EQ(http.redirect_calls, 1);
    EXPECT_EQ(http.open_calls, 2);
    EXPECT_EQ(http.close_calls, 2);
    EXPECT_EQ(http.cleanup_calls, 1);

    heap_caps_free(jpeg);
}

TEST(ClockBackgroundDownload, ReadsPaletteHeadersFromImageResponse)
{
    auto& http = espHttpClientStubState();
    http.reset();
    http.responses[0].status_code = 200;
    http.responses[0].content_length = sizeof(kMinimalJpeg);
    http.responses[0].body = kMinimalJpeg;
    http.responses[0].body_size = sizeof(kMinimalJpeg);
    http.responses[0].headers[0] = {"X-Clock-Fg", "#112233"};
    http.responses[0].headers[1] = {"X-Clock-Dim", "445566"};
    http.responses[0].headers[2] = {"X-Clock-Muted", "#778899"};
    http.responses[0].headers[3] = {"X-Clock-Faint", "aabbcc"};
    http.responses[0].headers[4] = {"X-Clock-Accent", "#ddeeff"};

    uint32_t size = 0;
    ClockForegroundPalette palette{};
    uint8_t* jpeg = downloadBackgroundJpeg("http://example.test/random-clock", &size, &palette);

    ASSERT_NE(jpeg, nullptr);
    EXPECT_EQ(palette.fg, 0x112233u);
    EXPECT_EQ(palette.dim, 0x445566u);
    EXPECT_EQ(palette.muted, 0x778899u);
    EXPECT_EQ(palette.faint, 0xaabbccu);
    EXPECT_EQ(palette.accent, 0xddeeffu);

    heap_caps_free(jpeg);
}

TEST(ClockBackgroundConfig, DefaultsToFiveMinuteRefreshAndDefaultUrl)
{
    ClockBackgroundService service;

    const ClockBackgroundConfig config = service.configSnapshot();

    EXPECT_STREQ(config.url, "http://esp32-bg.lan/random-clock");
    EXPECT_EQ(config.refresh_interval_ms, 5u * 60u * 1000u);
}

TEST(ClockBackgroundConfig, ConfigureOverridesUrlAndInterval)
{
    ClockBackgroundService service;

    service.configure({"http://example.test/custom-clock", 12345u});
    const ClockBackgroundConfig config = service.configSnapshot();

    EXPECT_STREQ(config.url, "http://example.test/custom-clock");
    EXPECT_EQ(config.refresh_interval_ms, 12345u);
}

TEST(ClockBackgroundRefreshPolicy, StartsImmediatelyThenWaitsForInterval)
{
    ClockBackgroundService service;
    service.configure({"http://example.test/random-clock", 5u * 60u * 1000u});

    EXPECT_TRUE(service.requestRefreshIfDue(1000u));
    EXPECT_TRUE(service.loading_);
    EXPECT_EQ(service.last_request_ms_, 1000u);

    service.loading_ = false;
    service.status_ = ClockBackgroundStatus::Error;
    EXPECT_FALSE(service.requestRefreshIfDue(1000u + 5u * 60u * 1000u - 1u));
    EXPECT_FALSE(service.loading_);

    EXPECT_TRUE(service.requestRefreshIfDue(1000u + 5u * 60u * 1000u));
    EXPECT_TRUE(service.loading_);
    EXPECT_EQ(service.last_request_ms_, 1000u + 5u * 60u * 1000u);
}

TEST(ClockBackgroundRefreshPolicy, DoesNotStartConcurrentDownload)
{
    ClockBackgroundService service;
    service.configure({"http://example.test/random-clock", 5u * 60u * 1000u});
    service.loading_ = true;
    service.status_ = ClockBackgroundStatus::Loading;

    EXPECT_FALSE(service.requestRefreshIfDue(1000u));
}

TEST(ClockBackgroundRefreshPolicy, ConfigureMakesNextCheckDueImmediately)
{
    ClockBackgroundService service;
    service.configure({"http://example.test/first", 5u * 60u * 1000u});

    EXPECT_TRUE(service.requestRefreshIfDue(1000u));
    service.loading_ = false;
    service.status_ = ClockBackgroundStatus::Ready;

    EXPECT_FALSE(service.requestRefreshIfDue(2000u));

    service.configure({"http://example.test/second", 5u * 60u * 1000u});
    EXPECT_TRUE(service.requestRefreshIfDue(2000u));
}
