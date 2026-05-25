#include "../main/app/services/mqtt_service.cpp"

#include <stdio.h>
#include <string.h>

namespace MusicMqtt {
void init()
{
}

bool takeCover(CoverImage*)
{
    return false;
}
} // namespace MusicMqtt

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

    MusicState state{};
    applyShairportField(state, "title", "Live Track", 10);
    applyShairportField(state, "artist", "Artist", 6);
    applyShairportField(state, "album", "Album", 5);
    applyShairportField(state, "genre", "Genre", 5);
    applyShairportField(state, "active", "1", 1);
    applyShairportField(state, "playing", "0", 1);
    applyShairportField(state, "volume", "-24.0", 5);
    applyShairportField(state, "ssnc/prgr", "1000/1500/2000", 14);

    failures += expect(strcmp(state.title, "Live Track") == 0, "title parse failed");
    failures += expect(strcmp(state.artist, "Artist") == 0, "artist parse failed");
    failures += expect(strcmp(state.album, "Album") == 0, "album parse failed");
    failures += expect(strcmp(state.genre, "Genre") == 0, "genre parse failed");
    failures += expect(state.active && !state.playing, "boolean parse failed");
    failures += expect(state.volume_percent == 50, "volume parse failed");
    failures += expect(state.progress_start_frame == 1000, "progress start parse failed");
    failures += expect(state.progress_current_frame == 1500, "progress current parse failed");
    failures += expect(state.progress_end_frame == 2000, "progress end parse failed");
    failures += expect(state.revision == 0, "pure parser should not bump revision");

    EventBus::get().resetForTest();
    MqttService& service = MqttService::get();
    const MusicState before = service.snapshot();
    failures += expect(service.applyField("title", "Changed", 7), "service apply should report changed field");

    const MusicState after = service.snapshot();
    failures += expect(strcmp(after.title, "Changed") == 0, "service title snapshot failed");
    failures += expect(after.revision == before.revision + 1, "service revision should increment");

    AppEvent event{};
    failures += expect(EventBus::get().poll(&event), "service apply should publish event");
    failures += expect(event.type == AppEventType::MusicStateChanged, "service event type mismatch");
    failures += expect(event.payload.music_state.revision == after.revision, "service event revision mismatch");
    failures += expect(!service.applyField("cover", "ignored", 7), "cover field should not change music state");
    failures += expect(!EventBus::get().poll(&event), "cover field should not publish music event");

    return failures == 0 ? 0 : 1;
}
