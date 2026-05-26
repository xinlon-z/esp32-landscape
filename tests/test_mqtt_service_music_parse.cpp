#include "app/services/shairport_parser.h"

#include <gtest/gtest.h>

TEST(MqttServiceMusicParse, ShairportFields)
{
    MusicState state{};
    applyShairportField(state, "title", "Live Track", 10);
    applyShairportField(state, "artist", "Artist", 6);
    applyShairportField(state, "album", "Album", 5);
    applyShairportField(state, "genre", "Genre", 5);
    applyShairportField(state, "active", "1", 1);
    applyShairportField(state, "playing", "0", 1);
    applyShairportField(state, "volume", "-24.0", 5);
    applyShairportField(state, "ssnc/prgr", "1000/1500/2000", 14);

    EXPECT_TRUE(strcmp(state.title, "Live Track") == 0) << "title parse failed";
    EXPECT_TRUE(strcmp(state.artist, "Artist") == 0) << "artist parse failed";
    EXPECT_TRUE(strcmp(state.album, "Album") == 0) << "album parse failed";
    EXPECT_TRUE(strcmp(state.genre, "Genre") == 0) << "genre parse failed";
    EXPECT_TRUE(state.active && !state.playing) << "boolean parse failed";
    EXPECT_TRUE(state.volume_percent == 50) << "volume parse failed";
    EXPECT_TRUE(state.progress_start_frame == 1000) << "progress start parse failed";
    EXPECT_TRUE(state.progress_current_frame == 1500) << "progress current parse failed";
    EXPECT_TRUE(state.progress_end_frame == 2000) << "progress end parse failed";
    EXPECT_TRUE(state.revision == 0) << "pure parser should not bump revision";
}
