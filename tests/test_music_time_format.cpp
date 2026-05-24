#include "../main/music_time_format.h"

#include <stdio.h>
#include <string.h>

static int expectTime(const char* name, uint32_t elapsed, uint32_t duration, const char* expected)
{
    char actual[24];
    formatMusicTimeDisplay(elapsed, duration, actual, sizeof(actual));
    if (strcmp(actual, expected) != 0) {
        printf("%s expected %s got %s\n", name, expected, actual);
        return 1;
    }
    return 0;
}

int main()
{
    int failures = 0;
    failures += expectTime("normal duration", 0, 214, "0:00/3:34");
    failures += expectTime("multi minute elapsed", 73, 214, "1:13/3:34");
    return failures == 0 ? 0 : 1;
}
