#include "app/services/mqtt_service.cpp"
#include "app/services/cover_service.cpp"
#include "app/core/event/event_bus.cpp"

#include "esp_heap_caps.h"

#include <gtest/gtest.h>

namespace {
uint8_t* pending_cover = nullptr;
uint32_t pending_cover_size = 0;
} // namespace

namespace MusicMqtt {
void init()
{
}

bool takeCover(CoverImage* cover)
{
    if (!cover || !pending_cover || pending_cover_size == 0) {
        return false;
    }
    cover->data = pending_cover;
    cover->size = pending_cover_size;
    pending_cover = nullptr;
    pending_cover_size = 0;
    return true;
}
} // namespace MusicMqtt

TEST(MqttServiceCoverPump, PumpAndDecode)
{
    CoverService::get().clear();
    EventBus::get().resetForTest();

    pending_cover_size = 4;
    pending_cover = static_cast<uint8_t*>(heap_caps_malloc(pending_cover_size, MALLOC_CAP_8BIT));
    pending_cover[0] = 0xff;
    pending_cover[1] = 0xd8;
    pending_cover[2] = 0xff;
    pending_cover[3] = 0xd9;

    EXPECT_TRUE(MqttService::get().pumpPendingCover()) << "pumpPendingCover should consume pending cover";

    CoverState active = CoverService::get().active();
    EXPECT_TRUE(active.cover_id == 1) << "first cover_id should be 1";
    EXPECT_TRUE(active.status == CoverStatus::Ready) << "cover status should become Ready after decode";
    EXPECT_TRUE(active.jpeg_size == 4) << "CoverService jpeg size mismatch";
    EXPECT_TRUE(active.has_pixels) << "CoverService should expose decoded pixels";

    BorrowedCover borrowed{};
    EXPECT_TRUE(CoverService::get().borrow(active.cover_id, &borrowed)) << "CoverService should lend active cover";
    EXPECT_TRUE(borrowed.jpeg_data != nullptr) << "borrowed cover should expose jpeg bytes";
    EXPECT_TRUE(borrowed.jpeg_size == 4) << "borrowed cover size mismatch";
    EXPECT_TRUE(borrowed.image != nullptr) << "borrowed cover should expose image descriptor";
    EXPECT_TRUE(borrowed.pixels != nullptr) << "borrowed cover should expose decoded pixels";

    AppEvent event{};
    EXPECT_TRUE(EventBus::get().poll(&event)) << "CoverService should publish loading CoverStateChanged";
    EXPECT_TRUE(event.type == AppEventType::CoverStateChanged) << "cover event type mismatch";
    EXPECT_TRUE(event.payload.cover_state.cover_id == active.cover_id) << "cover event id mismatch";
    EXPECT_TRUE(event.payload.cover_state.status == CoverStatus::Loading) << "cover event status mismatch";
    EXPECT_TRUE(EventBus::get().poll(&event)) << "CoverService should publish ready CoverStateChanged";
    EXPECT_TRUE(event.type == AppEventType::CoverStateChanged) << "ready cover event type mismatch";
    EXPECT_TRUE(event.payload.cover_state.cover_id == active.cover_id) << "ready cover event id mismatch";
    EXPECT_TRUE(event.payload.cover_state.status == CoverStatus::Ready) << "ready cover event status mismatch";

    pending_cover_size = 4;
    pending_cover = static_cast<uint8_t*>(heap_caps_malloc(pending_cover_size, MALLOC_CAP_8BIT));
    pending_cover[0] = 0xff;
    pending_cover[1] = 0xd8;
    pending_cover[2] = 0xff;
    pending_cover[3] = 0xd9;

    EXPECT_TRUE(MqttService::get().pumpPendingCover()) << "second pumpPendingCover should consume pending cover";
    active = CoverService::get().active();
    EXPECT_TRUE(active.cover_id == 2) << "second cover_id should increment";
    EXPECT_TRUE(active.status == CoverStatus::Ready) << "second cover should become Ready";

    CoverService::get().clear();
}
