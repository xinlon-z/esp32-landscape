#include "app/features/music/music_model.cpp"
#include "app/core/event/event_bus.cpp"

#include <gtest/gtest.h>

TEST(MusicModel, BuildDisplayState)
{
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

    EXPECT_TRUE(strcmp(display.title, "Track") == 0) << "title should copy state title";
    EXPECT_TRUE(strcmp(display.subtitle, "Artist - Album") == 0) << "subtitle should join artist and album";
    EXPECT_TRUE(strcmp(display.time, "0:42/2:05") == 0) << "time should format elapsed and duration";
    EXPECT_TRUE(display.progress_per_mille == 336) << "progress should be per-mille";
    EXPECT_TRUE(display.playing) << "playing should copy state playing";

    state.artist[0] = '\0';
    snprintf(state.album, sizeof(state.album), "Album Only");
    display = model.build(state, 200 * kFramesPerSecond);
    EXPECT_TRUE(strcmp(display.subtitle, "Album Only") == 0) << "subtitle should fall back to album";
    EXPECT_TRUE(strcmp(display.time, "2:05/2:05") == 0) << "elapsed should clamp to duration";
    EXPECT_TRUE(display.progress_per_mille == 1000) << "progress should clamp at 1000";
}
