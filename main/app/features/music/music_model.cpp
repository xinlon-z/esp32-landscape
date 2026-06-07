#include "music_model.h"

#include "util/music_time_format.h"

#include <stdio.h>
#include <string.h>

namespace {
constexpr uint32_t kFramesPerSecond = 44100;

uint32_t totalFrames(const MusicState& state)
{
    if (state.progress_end_frame <= state.progress_start_frame) {
        return 0;
    }
    return state.progress_end_frame - state.progress_start_frame;
}

uint32_t durationSeconds(const MusicState& state)
{
    return totalFrames(state) / kFramesPerSecond;
}
} // namespace

MusicDisplayState MusicModel::build(const MusicState& state, uint32_t elapsed_frames)
{
    MusicDisplayState display{};
    snprintf(display.title, sizeof(display.title), "%s", state.title);

    if (state.artist[0] && state.album[0]) {
        snprintf(display.subtitle, sizeof(display.subtitle), "%s - %s", state.artist, state.album);
    } else if (state.artist[0]) {
        snprintf(display.subtitle, sizeof(display.subtitle), "%s", state.artist);
    } else {
        snprintf(display.subtitle, sizeof(display.subtitle), "%s", state.album);
    }

    const uint32_t total = totalFrames(state);
    uint32_t clamped_elapsed = elapsed_frames;
    if (total == 0 || clamped_elapsed > total) {
        clamped_elapsed = total;
    }

    const uint32_t progress = total == 0 ? 0 : static_cast<uint32_t>(
        (static_cast<uint64_t>(clamped_elapsed) * 1000u) / total);
    display.progress_per_mille = progress > 1000u ? 1000u : progress;
    display.playing = state.playing;
    formatMusicTimeDisplay(clamped_elapsed / kFramesPerSecond,
                           durationSeconds(state),
                           display.time,
                           sizeof(display.time));
    return display;
}
