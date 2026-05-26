#include <gtest/gtest.h>
#include "app/features/music/music_presenter.h"
#include "app/core/event/event_bus.h"
#include "app/services/mqtt_service.h"
#include "app/services/cover_service.h"

#include "esp_heap_caps.h"

namespace {
int render_count = 0;
int cover_placeholder_count = 0;
int cover_render_count = 0;
int take_cover_count = 0;
} // namespace

namespace MusicMqtt {
struct CoverImage {
    uint8_t* data = nullptr;
    uint32_t size = 0;
};

void init() {}
bool takeCover(CoverImage*)
{
    ++take_cover_count;
    return false;
}
} // namespace MusicMqtt

void MusicView::create() {}
void MusicView::destroy() {}
void MusicView::render(const MusicDisplayState&) { ++render_count; }
void MusicView::renderCover(const BorrowedCover&) { ++cover_render_count; }
void MusicView::renderCoverPlaceholder() { ++cover_placeholder_count; }

TEST(MusicPresenterEventBus, TickAndCoverRender)
{
    EventBus::get().resetForTest();
    CoverService::get().clear();

    EXPECT_TRUE(MqttService::get().applyField("title", "Queued", 6))
        << "initial music update should publish an event";

    MusicView view;
    MusicPresenter presenter(view);
    presenter.start();

    AppEvent event{};
    EXPECT_TRUE(EventBus::get().poll(&event))
        << "start should not consume queued music event";
    EXPECT_TRUE(event.type == AppEventType::MusicStateChanged)
        << "queued event should remain a music event";
    EXPECT_TRUE(take_cover_count == 0)
        << "start should not pump pending covers";

    EXPECT_TRUE(MqttService::get().applyField("title", "Ticked", 6))
        << "second music update should publish an event";
    const int placeholder_before_cover_event = cover_placeholder_count;
    uint8_t* cover = static_cast<uint8_t*>(heap_caps_malloc(128, MALLOC_CAP_8BIT));
    EXPECT_TRUE(cover != nullptr) << "test cover allocation should succeed";
    if (cover) {
        cover[0] = 0xff;
        cover[1] = 0xd8;
        EXPECT_TRUE(CoverService::get().acceptJpeg(cover, 128) != 0)
            << "cover service should publish a cover event";
    }

    presenter.tick();
    EXPECT_TRUE(!EventBus::get().poll(&event))
        << "tick should consume queued presenter events";
    EXPECT_TRUE(take_cover_count == 0)
        << "tick should not pump pending covers";
    EXPECT_TRUE(render_count >= 2) << "presenter should render on start and tick";
    EXPECT_TRUE(cover_placeholder_count >= 1) << "start should render cover snapshot";
    EXPECT_TRUE(cover_placeholder_count == placeholder_before_cover_event)
        << "ready cover should not render another placeholder";
    EXPECT_TRUE(cover_render_count == 1)
        << "tick should render decoded service-owned cover";

    presenter.stop();
    CoverService::get().clear();
}
