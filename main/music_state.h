#pragma once

#include "app/features/music/music_state.h"

#include <stddef.h>
#include <stdint.h>

uint8_t musicProgressPercent(const MusicState& state);
uint32_t musicElapsedSeconds(const MusicState& state);
uint32_t musicDurationSeconds(const MusicState& state);
void musicStateApplyField(MusicState& state, const char* field, const char* payload, size_t payload_len);
