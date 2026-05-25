#include "mqtt_service.h"

#include "cover_service.h"
#include "../core/event/event_bus.h"
#include "../../music_mqtt.h"

#include <string.h>

#include <mutex>

namespace {
bool samePayload(const MusicState& left, const MusicState& right)
{
    return left.active == right.active &&
           left.playing == right.playing &&
           strcmp(left.title, right.title) == 0 &&
           strcmp(left.artist, right.artist) == 0 &&
           strcmp(left.album, right.album) == 0 &&
           strcmp(left.genre, right.genre) == 0 &&
           left.volume_percent == right.volume_percent &&
           left.progress_start_frame == right.progress_start_frame &&
           left.progress_current_frame == right.progress_current_frame &&
           left.progress_end_frame == right.progress_end_frame &&
           left.last_progress_ms == right.last_progress_ms;
}
} // namespace

MqttService& MqttService::get()
{
    static MqttService service;
    return service;
}

void MqttService::init()
{
    MusicMqtt::init();
}

MusicState MqttService::snapshot()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

bool MqttService::pumpPendingCover()
{
    MusicMqtt::CoverImage cover{};
    if (!MusicMqtt::takeCover(&cover)) {
        return false;
    }
    const uint32_t cover_id = CoverService::get().acceptJpeg(cover.data, cover.size);
    return cover_id != 0;
}

bool MqttService::applyField(const char* field, const char* payload, size_t payload_len, uint32_t last_progress_ms)
{
    if (!field || strcmp(field, "cover") == 0) {
        return false;
    }

    uint32_t revision = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        MusicState next = state_;
        applyShairportField(next, field, payload, payload_len);
        if (strcmp(field, "ssnc/prgr") == 0) {
            next.last_progress_ms = last_progress_ms;
        }
        if (samePayload(state_, next)) {
            return false;
        }

        next.revision = state_.revision + 1;
        state_ = next;
        revision = state_.revision;
    }

    publishChanged(revision);
    return true;
}

void MqttService::publishChanged(uint32_t revision)
{
    AppEvent event{};
    event.type = AppEventType::MusicStateChanged;
    event.payload.music_state.revision = revision;
    EventBus::get().publish(event);
}
