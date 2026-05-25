#include "../main/music_mqtt.cpp"

#include <stdio.h>
#include <string.h>

#include "../main/app/core/event/event_bus.h"
#include "../main/app/services/mqtt_service.h"

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

    return failures == 0 ? 0 : 1;
}
