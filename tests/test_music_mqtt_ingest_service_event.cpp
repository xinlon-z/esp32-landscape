#include "../main/music_mqtt.cpp"

#include <stdio.h>
#include <string.h>

#include "../main/app/core/event/event_bus.h"
#include "../main/app/services/cover_service.h"
#include "../main/app/services/mqtt_service.h"
#include "esp_heap_caps.h"

namespace ClockNet {
Status getStatus()
{
    return {};
}
} // namespace ClockNet

static int expect(bool condition, const char* message)
{
    if (!condition) {
        printf("%s\n", message);
        return 1;
    }
    return 0;
}

int main()
{
    int failures = 0;

    MusicMqtt::init();
    EventBus::get().resetForTest();
    const MusicState before = MqttService::get().snapshot();

    updateState("title", "Ingested", 8);

    const MusicState service_state = MqttService::get().snapshot();
    failures += expect(strcmp(service_state.title, "Ingested") == 0, "service snapshot did not receive ingest");
    failures += expect(service_state.revision == before.revision + 1, "ingest should increment service revision");

    MusicState legacy_state{};
    failures += expect(MusicMqtt::getState(&legacy_state), "legacy getState should still report state");
    failures += expect(strcmp(legacy_state.title, "Ingested") == 0, "legacy state did not mirror service ingest");
    failures += expect(legacy_state.revision == service_state.revision, "legacy state revision mismatch");

    AppEvent event{};
    failures += expect(EventBus::get().poll(&event), "ingest should publish MusicStateChanged");
    failures += expect(event.type == AppEventType::MusicStateChanged, "ingest event type mismatch");
    failures += expect(event.payload.music_state.revision == service_state.revision, "ingest event revision mismatch");

    CoverService::get().clear();
    EventBus::get().resetForTest();

    uint8_t* cover = static_cast<uint8_t*>(heap_caps_malloc(128, MALLOC_CAP_8BIT));
    cover[0] = 0xff;
    cover[1] = 0xd8;
    for (int i = 2; i < 128; ++i) {
        cover[i] = static_cast<uint8_t>(i);
    }

    updateCover(cover, 128);

    CoverState active = CoverService::get().active();
    failures += expect(active.cover_id == 1, "cover ingest should assign first cover_id");
    failures += expect(active.status == CoverStatus::Loading, "cover ingest should set Loading");
    failures += expect(active.jpeg_size == 128, "cover ingest size mismatch");

    BorrowedCover borrowed{};
    failures += expect(CoverService::get().borrow(active.cover_id, &borrowed), "cover ingest should lend active cover");
    failures += expect(borrowed.jpeg_data != nullptr, "cover ingest should expose borrowed bytes");
    failures += expect(borrowed.jpeg_size == 128, "borrowed cover ingest size mismatch");

    failures += expect(EventBus::get().poll(&event), "cover ingest should publish CoverStateChanged");
    failures += expect(event.type == AppEventType::CoverStateChanged, "cover ingest event type mismatch");
    failures += expect(event.payload.cover_state.cover_id == active.cover_id, "cover ingest event id mismatch");
    failures += expect(event.payload.cover_state.status == CoverStatus::Loading, "cover ingest event status mismatch");

    CoverService::get().clear();
    return failures == 0 ? 0 : 1;
}
