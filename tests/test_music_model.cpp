#include "../main/app/features/music/music_model.h"

#include <stdio.h>
#include <string.h>

namespace {
constexpr uint32_t kFramesPerSecond = 44100;

int expect(bool condition, const char* message)
{
    if (!condition) {
        printf("%s\n", message);
        return 1;
    }
    return 0;
}
} // namespace

int main()
{
    int failures = 0;

    MusicState state{};
    state.playing = true;
    snprintf(state.title, sizeof(state.title), "Track");
    snprintf(state.artist, sizeof(state.artist), "Artist");
    snprintf(state.album, sizeof(state.album), "Album");
    state.progress_start_frame = 1000;
    state.progress_current_frame = 1000 + 42 * kFramesPerSecond;
    state.progress_end_frame = 1000 + 125 * kFramesPerSecond;

    MusicModel model;
    MusicDisplayState display = model.build(state, 42 * kFramesPerSecond);

    failures += expect(strcmp(display.title, "Track") == 0, "title should copy state title");
    failures += expect(strcmp(display.subtitle, "Artist - Album") == 0, "subtitle should join artist and album");
    failures += expect(strcmp(display.time, "0:42/2:05") == 0, "time should format elapsed and duration");
    failures += expect(display.progress_per_mille == 336, "progress should be per-mille");
    failures += expect(display.playing, "playing should copy state playing");

    state.artist[0] = '\0';
    snprintf(state.album, sizeof(state.album), "Album Only");
    display = model.build(state, 200 * kFramesPerSecond);
    failures += expect(strcmp(display.subtitle, "Album Only") == 0, "subtitle should fall back to album");
    failures += expect(strcmp(display.time, "2:05/2:05") == 0, "elapsed should clamp to duration");
    failures += expect(display.progress_per_mille == 1000, "progress should clamp at 1000");

    return failures == 0 ? 0 : 1;
}
