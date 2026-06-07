#include <gtest/gtest.h>
#include "platform/music_mqtt.cpp"

#include "app/core/event/event_bus.h"
#include "app/services/cover_service.h"
#include "app/services/mqtt_service.h"
#include "esp_heap_caps.h"

namespace ClockNet {
Status getStatus()
{
    return {};
}
} // namespace ClockNet

TEST(MusicMqttIngest, MetadataAndCoverUseSeparateSubscriptions)
{
    EXPECT_EQ(kMetadataStream.kind, StreamKind::Metadata);
    EXPECT_EQ(kCoverStream.kind, StreamKind::Cover);

    for (size_t i = 0; i < kMetadataStream.topic_count; ++i) {
        EXPECT_TRUE(strstr(kMetadataStream.topics[i], "/cover") == nullptr)
            << "metadata stream must not receive large cover payloads";
        EXPECT_TRUE(strchr(kMetadataStream.topics[i], '#') == nullptr)
            << "metadata stream must avoid wildcard subscriptions";
    }

    ASSERT_EQ(kCoverStream.topic_count, 1u);
    EXPECT_TRUE(strstr(kCoverStream.topics[0], "/cover") != nullptr);
}

TEST(MusicMqttIngest, StateAndCoverEvents)
{
    MusicMqtt::init();
    EventBus::get().resetForTest();
    const MusicState before = MqttService::get().snapshot();

    updateState("title", "Ingested", 8);

    const MusicState service_state = MqttService::get().snapshot();
    EXPECT_STREQ(service_state.title, "Ingested") << "service snapshot did not receive ingest";
    EXPECT_EQ(service_state.revision, before.revision + 1) << "ingest should increment service revision";

    AppEvent event{};
    EXPECT_TRUE(EventBus::get().poll(&event)) << "ingest should publish MusicStateChanged";
    EXPECT_EQ(event.type, AppEventType::MusicStateChanged) << "ingest event type mismatch";
    EXPECT_EQ(event.payload.music_state.revision, service_state.revision) << "ingest event revision mismatch";

    CoverService::get().clear();
    EventBus::get().resetForTest();

    uint8_t* cover = static_cast<uint8_t*>(heap_caps_malloc(128, MALLOC_CAP_8BIT));
    cover[0] = 0xff;
    cover[1] = 0xd8;
    for (int i = 2; i < 128; ++i) {
        cover[i] = static_cast<uint8_t>(i);
    }

    updateCover(cover, 128);
    CoverService::get().tickDecodeForTest();

    CoverState active = CoverService::get().active();
    EXPECT_EQ(active.cover_id, 1) << "cover ingest should assign first cover_id";
    EXPECT_EQ(active.status, CoverStatus::Ready) << "cover ingest should decode to Ready";
    EXPECT_EQ(active.jpeg_size, 128) << "cover ingest size mismatch";
    EXPECT_TRUE(active.has_pixels) << "cover ingest should expose decoded pixels";

    BorrowedCover borrowed{};
    EXPECT_TRUE(CoverService::get().borrow(active.cover_id, &borrowed)) << "cover ingest should lend active cover";
    EXPECT_TRUE(borrowed.jpeg_data != nullptr) << "cover ingest should expose borrowed bytes";
    EXPECT_EQ(borrowed.jpeg_size, 128) << "borrowed cover ingest size mismatch";
    EXPECT_TRUE(borrowed.image != nullptr) << "cover ingest should expose image descriptor";
    EXPECT_TRUE(borrowed.pixels != nullptr) << "cover ingest should expose decoded pixels";

    auto* copied_pixels = static_cast<lv_color_t*>(
        heap_caps_malloc(CoverService::kCoverPixelCount * sizeof(lv_color_t), MALLOC_CAP_8BIT));
    ASSERT_TRUE(copied_pixels != nullptr) << "test cover copy allocation should succeed";
    lv_img_dsc_t copied_image{};
    EXPECT_TRUE(CoverService::get().copyPixels(active.cover_id,
                                               copied_pixels,
                                               CoverService::kCoverPixelCount,
                                               &copied_image))
        << "cover service should copy pixels under its own lock for UI-owned buffers";
    EXPECT_EQ(copied_image.header.w, CoverService::kCoverSize);
    EXPECT_EQ(copied_image.header.h, CoverService::kCoverSize);
    EXPECT_EQ(copied_image.data, reinterpret_cast<const uint8_t*>(copied_pixels));
    heap_caps_free(copied_pixels);

    EXPECT_TRUE(EventBus::get().poll(&event)) << "cover ingest should publish loading CoverStateChanged";
    EXPECT_EQ(event.type, AppEventType::CoverStateChanged) << "cover ingest event type mismatch";
    EXPECT_EQ(event.payload.cover_state.cover_id, active.cover_id) << "cover ingest event id mismatch";
    EXPECT_EQ(event.payload.cover_state.status, CoverStatus::Loading) << "cover ingest event status mismatch";
    EXPECT_TRUE(EventBus::get().poll(&event)) << "cover ingest should publish ready CoverStateChanged";
    EXPECT_EQ(event.type, AppEventType::CoverStateChanged) << "cover ready event type mismatch";
    EXPECT_EQ(event.payload.cover_state.cover_id, active.cover_id) << "cover ready event id mismatch";
    EXPECT_EQ(event.payload.cover_state.status, CoverStatus::Ready) << "cover ready event status mismatch";

    CoverService::get().clear();
}

TEST(MusicMqttIngest, ProgressTimestampUsesLvglTickDomain)
{
    EventBus::get().resetForTest();

    xTaskGetTickCountStubValue() = 5000;
    lvTickGetStubValue() = 1200;

    const char* progress = "1000/1000/4411000";
    updateState("ssnc/prgr", progress, strlen(progress));

    const MusicState state = MqttService::get().snapshot();
    EXPECT_EQ(state.last_progress_ms, 1200u)
        << "progress timestamps are consumed with lv_tick_elaps(), so ingest must store lv_tick_get()";

    xTaskGetTickCountStubValue() = 0;
    lvTickGetStubValue() = 0;
}
