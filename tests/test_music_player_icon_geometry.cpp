#include "app/features/music/util/music_player_icon_geometry.h"

#include <gtest/gtest.h>

TEST(MusicPlayerIconGeometry, PlayPauseOffset)
{
    {
        const char* name = "pause icon";
        bool playing = true;
        int expected_x = 0;
        int expected_y = 0;
        EXPECT_EQ(musicPlayPauseIconOffset(playing).x, expected_x) << name;
        EXPECT_EQ(musicPlayPauseIconOffset(playing).y, expected_y) << name;
    }
    {
        const char* name = "play icon";
        bool playing = false;
        int expected_x = 2;
        int expected_y = 0;
        EXPECT_EQ(musicPlayPauseIconOffset(playing).x, expected_x) << name;
        EXPECT_EQ(musicPlayPauseIconOffset(playing).y, expected_y) << name;
    }
}
