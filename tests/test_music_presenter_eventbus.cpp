#include "../main/app/features/music/music_presenter.h"
#include "../main/app/core/event/event_bus.h"
#include "../main/app/services/mqtt_service.h"
#include "../main/app/services/cover_service.h"

#include "esp_heap_caps.h"

#include <stdio.h>

namespace {
int render_count = 0;
int cover_placeholder_count = 0;
int cover_render_count = 0;
int take_cover_count = 0;

int expect(bool condition, const char* message)
{
    if (!condition) {
        printf("%s\n", message);
        return 1;
    }
    return 0;
}
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

int main()
{
    int failures = 0;
    EventBus::get().resetForTest();
    CoverService::get().clear();

    failures += expect(MqttService::get().applyField("title", "Queued", 6),
                       "initial music update should publish an event");

    MusicView view;
    MusicPresenter presenter(view);
    presenter.start();

    AppEvent event{};
    failures += expect(EventBus::get().poll(&event),
                       "start should not consume queued music event");
    failures += expect(event.type == AppEventType::MusicStateChanged,
                       "queued event should remain a music event");
    failures += expect(take_cover_count == 0,
                       "start should not pump pending covers");

    failures += expect(MqttService::get().applyField("title", "Ticked", 6),
                       "second music update should publish an event");
    const int placeholder_before_cover_event = cover_placeholder_count;
    uint8_t* cover = static_cast<uint8_t*>(heap_caps_malloc(128, MALLOC_CAP_8BIT));
    failures += expect(cover != nullptr, "test cover allocation should succeed");
    if (cover) {
        cover[0] = 0xff;
        cover[1] = 0xd8;
        failures += expect(CoverService::get().acceptJpeg(cover, 128) != 0,
                           "cover service should publish a cover event");
    }

    presenter.tick();
    failures += expect(!EventBus::get().poll(&event),
                       "tick should consume queued presenter events");
    failures += expect(take_cover_count == 0,
                       "tick should not pump pending covers");
    failures += expect(render_count >= 2, "presenter should render on start and tick");
    failures += expect(cover_placeholder_count >= 1, "start should render cover snapshot");
    failures += expect(cover_placeholder_count == placeholder_before_cover_event,
                       "ready cover should not render another placeholder");
    failures += expect(cover_render_count == 1,
                       "tick should render decoded service-owned cover");

    presenter.stop();
    CoverService::get().clear();
    return failures == 0 ? 0 : 1;
}
