#include "app/features/music/util/music_time_format.h"

#include <gtest/gtest.h>

TEST(MusicTimeFormat, FormatElapsedDuration)
{
    {
        const char* name = "normal duration";
        uint32_t elapsed = 0;
        uint32_t duration = 214;
        const char* expected = "0:00/3:34";
        char actual[24];
        formatMusicTimeDisplay(elapsed, duration, actual, sizeof(actual));
        EXPECT_STREQ(actual, expected) << name;
    }
    {
        const char* name = "multi minute elapsed";
        uint32_t elapsed = 73;
        uint32_t duration = 214;
        const char* expected = "1:13/3:34";
        char actual[24];
        formatMusicTimeDisplay(elapsed, duration, actual, sizeof(actual));
        EXPECT_STREQ(actual, expected) << name;
    }
}
