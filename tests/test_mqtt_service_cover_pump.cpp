#include <gtest/gtest.h>

// Expose private members so tests can call runOnePendingDecode() directly,
// mirroring the pattern used by test_background_blur_service.cpp. The worker
// task is a no-op in the stub environment, so tests drive the decode manually.
#define private public
#define protected public
#include "app/services/cover_service.cpp"
#include "app/services/mqtt_service.cpp"
#include "app/core/event/event_bus.cpp"
#undef private
#undef protected

#include "esp_heap_caps.h"

namespace MusicMqtt {
void init() {}
} // namespace MusicMqtt

namespace {
// Minimal JPEG: SOI + EOI, 4 bytes.
uint8_t* makeMinimalJpeg(uint32_t* out_size)
{
    *out_size = 4;
    auto* data = static_cast<uint8_t*>(heap_caps_malloc(*out_size, MALLOC_CAP_8BIT));
    data[0] = 0xff;
    data[1] = 0xd8;
    data[2] = 0xff;
    data[3] = 0xd9;
    return data;
}

uint32_t seedReadyCover()
{
    CoverService& service = CoverService::get();
    service.releaseActive();

    auto* pixels = static_cast<lv_color_t*>(
        heap_caps_malloc(CoverService::kCoverPixelCount * sizeof(lv_color_t), MALLOC_CAP_8BIT));
    if (!pixels) {
        return 0;
    }
    for (uint32_t i = 0; i < CoverService::kCoverPixelCount; ++i) {
        pixels[i] = lv_color_make(0x20, 0x40, 0x60);
    }

    const uint32_t cover_id = service.next_cover_id_ + 1;
    service.next_cover_id_ = cover_id;
    service.active_.cover_id = cover_id;
    service.active_.status = CoverStatus::Ready;
    service.active_.pixels = pixels;
    service.active_.image.header.always_zero = 0;
    service.active_.image.header.w = CoverService::kCoverSize;
    service.active_.image.header.h = CoverService::kCoverSize;
    service.active_.image.header.cf = LV_IMG_CF_TRUE_COLOR;
    service.active_.image.data = reinterpret_cast<const uint8_t*>(pixels);
    service.active_.image.data_size = CoverService::kCoverPixelCount * sizeof(lv_color_t);
    return cover_id;
}

bool copyDisplayCoverId(uint32_t* out_cover_id)
{
    auto* copied = static_cast<lv_color_t*>(
        heap_caps_malloc(CoverService::kCoverPixelCount * sizeof(lv_color_t), MALLOC_CAP_8BIT));
    if (!copied) {
        return false;
    }
    lv_img_dsc_t copied_image{};
    uint32_t copied_cover_id = 0;
    const bool ok = CoverService::get().copyDisplayPixels(copied,
                                                          CoverService::kCoverPixelCount,
                                                          &copied_image,
                                                          &copied_cover_id);
    heap_caps_free(copied);
    if (out_cover_id) {
        *out_cover_id = copied_cover_id;
    }
    return ok;
}
} // namespace

TEST(CoverServiceAsync, AcceptJpegReturnsImmediatelyAsLoading)
{
    CoverService::get().clear();
    EventBus::get().resetForTest();

    uint32_t size = 0;
    uint8_t* data = makeMinimalJpeg(&size);

    const uint32_t id = CoverService::get().acceptJpeg(data, size);
    EXPECT_GT(id, 0u);

    // Status must be Loading immediately — caller is not blocked.
    CoverState s = CoverService::get().active();
    EXPECT_EQ(s.cover_id, id);
    EXPECT_EQ(s.status, CoverStatus::Loading);

    AppEvent ev{};
    ASSERT_TRUE(EventBus::get().poll(&ev)) << "Loading event should publish immediately";
    EXPECT_EQ(ev.type, AppEventType::CoverStateChanged);
    EXPECT_EQ(ev.payload.cover_state.status, CoverStatus::Loading);
    EXPECT_EQ(ev.payload.cover_state.cover_id, id);

    // No Ready event yet — worker hasn't run.
    EXPECT_FALSE(EventBus::get().poll(&ev)) << "Ready must not fire before decode";

    // Drive the worker to clean up memory.
    CoverService::get().runOnePendingDecode();
}

TEST(CoverServiceAsync, DriveWorkerPublishesReadyOrError)
{
    CoverService::get().clear();
    EventBus::get().resetForTest();

    uint32_t size = 0;
    uint8_t* data = makeMinimalJpeg(&size);
    const uint32_t id = CoverService::get().acceptJpeg(data, size);

    // Drain the Loading event.
    AppEvent ev{};
    EventBus::get().poll(&ev);

    CoverService::get().runOnePendingDecode();

    // Status must be Ready or Error (minimal JPEG may not fully decode).
    CoverState s = CoverService::get().active();
    EXPECT_EQ(s.cover_id, id);
    EXPECT_TRUE(s.status == CoverStatus::Ready || s.status == CoverStatus::Error);

    // Post-decode event must have been published.
    ASSERT_TRUE(EventBus::get().poll(&ev)) << "post-decode event must be published";
    EXPECT_EQ(ev.payload.cover_state.cover_id, id);
    EXPECT_TRUE(ev.payload.cover_state.status == CoverStatus::Ready ||
                ev.payload.cover_state.status == CoverStatus::Error);
}

TEST(CoverServiceAsync, BorrowWorksAfterDecode)
{
    CoverService::get().clear();
    EventBus::get().resetForTest();

    uint32_t size = 0;
    uint8_t* data = makeMinimalJpeg(&size);
    const uint32_t id = CoverService::get().acceptJpeg(data, size);
    CoverService::get().runOnePendingDecode();

    BorrowedCover borrowed{};
    EXPECT_TRUE(CoverService::get().borrow(id, &borrowed));
    EXPECT_EQ(borrowed.cover_id, id);
}

TEST(CoverServiceAsync, CoverIdIncrementsPerAccept)
{
    CoverService::get().clear();
    EventBus::get().resetForTest();

    uint32_t s1 = 0, s2 = 0;
    uint8_t* d1 = makeMinimalJpeg(&s1);
    uint8_t* d2 = makeMinimalJpeg(&s2);

    const uint32_t id1 = CoverService::get().acceptJpeg(d1, s1);
    CoverService::get().runOnePendingDecode();
    const uint32_t id2 = CoverService::get().acceptJpeg(d2, s2);
    CoverService::get().runOnePendingDecode();

    EXPECT_GT(id2, id1);
}

TEST(CoverServiceAsync, SupersededDecodeIsDiscarded)
{
    CoverService::get().clear();
    EventBus::get().resetForTest();

    uint32_t s1 = 0, s2 = 0;
    uint8_t* d1 = makeMinimalJpeg(&s1);
    uint8_t* d2 = makeMinimalJpeg(&s2);

    const uint32_t id1 = CoverService::get().acceptJpeg(d1, s1);
    (void)id1;
    // Second cover supersedes the first before the worker runs.
    const uint32_t id2 = CoverService::get().acceptJpeg(d2, s2);
    CoverService::get().runOnePendingDecode();

    CoverState s = CoverService::get().active();
    EXPECT_EQ(s.cover_id, id2) << "active cover must be the latest accepted one";
}

TEST(CoverServiceAsync, ReadyCoverRemainsDisplayableWhileNextCoverLoads)
{
    CoverService::get().clear();
    EventBus::get().resetForTest();

    const uint32_t previous_id = seedReadyCover();
    ASSERT_GT(previous_id, 0u);

    uint32_t display_cover_id = 0;
    ASSERT_TRUE(copyDisplayCoverId(&display_cover_id));
    EXPECT_EQ(display_cover_id, previous_id);

    uint32_t next_size = 0;
    uint8_t* next_data = makeMinimalJpeg(&next_size);
    const uint32_t next_id = CoverService::get().acceptJpeg(next_data, next_size);
    EXPECT_GT(next_id, previous_id);

    CoverState loading = CoverService::get().active();
    EXPECT_EQ(loading.cover_id, next_id);
    EXPECT_EQ(loading.status, CoverStatus::Loading);

    ASSERT_TRUE(copyDisplayCoverId(&display_cover_id))
        << "previous ready cover should remain available while next JPEG decodes";
    EXPECT_EQ(display_cover_id, previous_id);

    auto* copied = static_cast<lv_color_t*>(
        heap_caps_malloc(CoverService::kCoverPixelCount * sizeof(lv_color_t), MALLOC_CAP_8BIT));
    ASSERT_TRUE(copied != nullptr);
    lv_img_dsc_t copied_image{};
    EXPECT_TRUE(CoverService::get().copyPixels(previous_id,
                                               copied,
                                               CoverService::kCoverPixelCount,
                                               &copied_image));
    EXPECT_EQ(copied_image.header.w, CoverService::kCoverSize);
    heap_caps_free(copied);

    CoverService::get().runOnePendingDecode();
    CoverState decoded = CoverService::get().active();
    ASSERT_TRUE(copyDisplayCoverId(&display_cover_id));
    EXPECT_EQ(display_cover_id,
              decoded.status == CoverStatus::Ready ? next_id : previous_id)
        << "failed next decode should keep showing the previous cover";
}

TEST(CoverServiceAsync, CopyDisplayPixelsSelectsAndCopiesVisibleCoverAtomically)
{
    CoverService::get().clear();
    EventBus::get().resetForTest();

    const uint32_t previous_id = seedReadyCover();
    ASSERT_GT(previous_id, 0u);

    uint32_t next_size = 0;
    uint8_t* next_data = makeMinimalJpeg(&next_size);
    const uint32_t next_id = CoverService::get().acceptJpeg(next_data, next_size);
    ASSERT_GT(next_id, previous_id);

    auto* copied = static_cast<lv_color_t*>(
        heap_caps_malloc(CoverService::kCoverPixelCount * sizeof(lv_color_t), MALLOC_CAP_8BIT));
    ASSERT_TRUE(copied != nullptr);

    lv_img_dsc_t copied_image{};
    uint32_t copied_cover_id = 0;
    EXPECT_TRUE(CoverService::get().copyDisplayPixels(copied,
                                                      CoverService::kCoverPixelCount,
                                                      &copied_image,
                                                      &copied_cover_id));
    EXPECT_EQ(copied_cover_id, previous_id);
    EXPECT_EQ(copied_image.header.w, CoverService::kCoverSize);
    EXPECT_EQ(copied_image.data, reinterpret_cast<const uint8_t*>(copied));

    heap_caps_free(copied);
    CoverService::get().runOnePendingDecode();
}
