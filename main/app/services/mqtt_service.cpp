#include "mqtt_service.h"

#include "cover_service.h"
#include "../core/event/event_bus.h"
#include "../../music_mqtt.h"

#include <stdlib.h>
#include <string.h>

#include <mutex>

namespace {
constexpr int kVolumeRangeDb = 48;

void copyPayload(char* dest, size_t dest_size, const char* payload, size_t payload_len)
{
    if (!dest || dest_size == 0) {
        return;
    }

    const size_t copy_len = payload_len < dest_size - 1 ? payload_len : dest_size - 1;
    memcpy(dest, payload, copy_len);
    dest[copy_len] = '\0';
}

bool payloadTruthy(const char* payload, size_t payload_len)
{
    return payload_len > 0 && (payload[0] == '1' || payload[0] == 't' || payload[0] == 'T' ||
                               payload[0] == 'y' || payload[0] == 'Y');
}

uint32_t parseFrame(const char*& cursor)
{
    char* end = nullptr;
    const unsigned long value = strtoul(cursor, &end, 10);
    cursor = end;
    if (*cursor == '/') {
        ++cursor;
    }
    return static_cast<uint32_t>(value);
}

int clampPercent(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

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

void applyShairportField(MusicState& state, const char* field, const char* payload, size_t payload_len)
{
    if (!field || !payload) {
        return;
    }

    if (strcmp(field, "title") == 0) {
        copyPayload(state.title, sizeof(state.title), payload, payload_len);
    } else if (strcmp(field, "artist") == 0) {
        copyPayload(state.artist, sizeof(state.artist), payload, payload_len);
    } else if (strcmp(field, "album") == 0) {
        copyPayload(state.album, sizeof(state.album), payload, payload_len);
    } else if (strcmp(field, "genre") == 0) {
        copyPayload(state.genre, sizeof(state.genre), payload, payload_len);
    } else if (strcmp(field, "active") == 0) {
        state.active = payloadTruthy(payload, payload_len);
    } else if (strcmp(field, "playing") == 0) {
        state.playing = payloadTruthy(payload, payload_len);
    } else if (strcmp(field, "volume") == 0 || strcmp(field, "ssnc/pvol") == 0) {
        char value[24];
        copyPayload(value, sizeof(value), payload, payload_len);
        const double volume_db = strtod(value, nullptr);
        state.volume_percent = clampPercent(static_cast<int>(((volume_db + kVolumeRangeDb) * 100.0) / kVolumeRangeDb));
    } else if (strcmp(field, "ssnc/prgr") == 0) {
        char value[48];
        copyPayload(value, sizeof(value), payload, payload_len);
        const char* cursor = value;
        state.progress_start_frame = parseFrame(cursor);
        state.progress_current_frame = parseFrame(cursor);
        state.progress_end_frame = parseFrame(cursor);
    }
}

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

bool MqttService::takeCover(uint8_t** data, uint32_t* size)
{
    if (!data || !size) {
        return false;
    }
    *data = nullptr;
    *size = 0;

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
