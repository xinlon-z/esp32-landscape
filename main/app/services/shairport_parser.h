#pragma once

#include "../features/music/music_state.h"

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace shairport_parser_detail {
constexpr int kVolumeRangeDb = 48;

inline void copyPayload(char* dest, size_t dest_size, const char* payload, size_t payload_len)
{
    if (!dest || dest_size == 0) {
        return;
    }

    const size_t copy_len = payload_len < dest_size - 1 ? payload_len : dest_size - 1;
    memcpy(dest, payload, copy_len);
    dest[copy_len] = '\0';
}

inline bool payloadTruthy(const char* payload, size_t payload_len)
{
    return payload_len > 0 && (payload[0] == '1' || payload[0] == 't' || payload[0] == 'T' ||
                               payload[0] == 'y' || payload[0] == 'Y');
}

inline bool parseFrame(const char*& cursor, uint32_t* out)
{
    if (!cursor || !out || *cursor == '\0') {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long value = strtoul(cursor, &end, 10);
    if (end == cursor || errno == ERANGE || value > UINT32_MAX) {
        return false;
    }
    cursor = end;
    if (*cursor == '/') {
        ++cursor;
    }
    *out = static_cast<uint32_t>(value);
    return true;
}

inline int clampPercent(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}
} // namespace shairport_parser_detail

inline void applyShairportField(MusicState& state, const char* field, const char* payload, size_t payload_len)
{
    if (!field || !payload) {
        return;
    }

    using namespace shairport_parser_detail;

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
        char* end = nullptr;
        const double volume_db = strtod(value, &end);
        if (end != value && isfinite(volume_db)) {
            state.volume_percent = clampPercent(static_cast<int>(((volume_db + kVolumeRangeDb) * 100.0) / kVolumeRangeDb));
        }
    } else if (strcmp(field, "ssnc/prgr") == 0) {
        char value[48];
        copyPayload(value, sizeof(value), payload, payload_len);
        const char* cursor = value;
        uint32_t start = 0;
        uint32_t current = 0;
        uint32_t end = 0;
        if (parseFrame(cursor, &start) &&
            parseFrame(cursor, &current) &&
            parseFrame(cursor, &end) &&
            start <= current && current <= end) {
            state.progress_start_frame = start;
            state.progress_current_frame = current;
            state.progress_end_frame = end;
        }
    }
}
