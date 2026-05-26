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
MusicDisplayState last_rendered{};
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
void MusicView::render(const MusicDisplayState& state)
{
    ++render_count;
    last_rendered = state;
}
void MusicView::renderCover(const BorrowedCover&) { ++cover_render_count; }
void MusicView::renderCoverPlaceholder() { ++cover_placeholder_count; }

// BackgroundWidget's ctor/dtor register with the singleton blur service in
// production. The presenter test stubs out the entire view, so we provide
// trivial replacements here to avoid linking the service.
BackgroundWidget::BackgroundWidget() {}
BackgroundWidget::~BackgroundWidget() {}

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

// Reproduces the bug where, with a 32-bit-only multiplication, the wall-clock
// portion of elapsed frames overflows after ~97 s and the displayed time
// snaps back to ~0:00 mid-song. Drives the presenter through MqttService
// state so we exercise the public render path; verifies the rendered time
// reflects the wall-clock advance even at long elapsed offsets.
TEST(MusicPresenterEventBus, ElapsedFramesNoOverflowAtLongPlayback)
{
    EventBus::get().resetForTest();
    CoverService::get().clear();

    constexpr uint32_t kFrameRate = 44100u;
    constexpr uint32_t kStart = 1000u;
    constexpr uint32_t kEnd = kStart + 240u * kFrameRate;  // 4-minute song

    char prgr[64];
    snprintf(prgr, sizeof(prgr), "%u/%u/%u", kStart, kStart, kEnd);
    constexpr uint32_t kPrgrTickMs = 100;
    MqttService::get().applyField("ssnc/prgr", prgr, strlen(prgr), kPrgrTickMs);
    MqttService::get().applyField("playing", "true", 4);

    MusicView view;
    MusicPresenter presenter(view);
    presenter.start();

    auto secondsAt = [&](uint32_t elapsed_ms) -> uint32_t {
        lvTickElapsStubValue() = elapsed_ms;
        presenter.tick();
        const char* slash = strchr(last_rendered.time, '/');
        if (!slash) {
            return UINT32_MAX;
        }
        unsigned mm = 0, ss = 0;
        sscanf(last_rendered.time, "%u:%u", &mm, &ss);
        return mm * 60u + ss;
    };

    EXPECT_EQ(secondsAt(60000), 60u) << "60 s in: should display 1:00";
    EXPECT_EQ(secondsAt(97000), 97u) << "97 s in: just before the 32-bit overflow boundary";
    EXPECT_EQ(secondsAt(97500), 97u)
        << "97.5 s in: with 64-bit math elapsed must keep climbing, not snap to ~0";
    EXPECT_EQ(secondsAt(120000), 120u) << "2 min in: time must reflect wall-clock advance";
    EXPECT_EQ(secondsAt(180000), 180u) << "3 min in: must not have wrapped";

    presenter.stop();
    lvTickElapsStubValue() = 0;
    CoverService::get().clear();
}
