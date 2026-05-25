#pragma once

#include "../features/music/music_state.h"

#include <stddef.h>
#include <stdint.h>
#include <mutex>

class MqttService {
public:
    static MqttService& get();

    void init();
    MusicState snapshot();
    bool takeCover(uint8_t** data, uint32_t* size);
    bool applyField(const char* field, const char* payload, size_t payload_len, uint32_t last_progress_ms = 0);

private:
    MqttService() = default;

    void publishChanged(uint32_t revision);

    mutable std::mutex mutex_;
    MusicState state_{};
};

void applyShairportField(MusicState& state, const char* field, const char* payload, size_t payload_len);
