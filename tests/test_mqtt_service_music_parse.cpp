#include "../main/app/services/shairport_parser.h"

#include <stdio.h>
#include <string.h>

static int expect(bool condition, const char* message)
{
    if (!condition) {
        printf("%s\n", message);
        return 1;
    }
    return 0;
}

int main()
{
    int failures = 0;

    MusicState state{};
    applyShairportField(state, "title", "Live Track", 10);
    applyShairportField(state, "artist", "Artist", 6);
    applyShairportField(state, "album", "Album", 5);
    applyShairportField(state, "genre", "Genre", 5);
    applyShairportField(state, "active", "1", 1);
    applyShairportField(state, "playing", "0", 1);
    applyShairportField(state, "volume", "-24.0", 5);
    applyShairportField(state, "ssnc/prgr", "1000/1500/2000", 14);

    failures += expect(strcmp(state.title, "Live Track") == 0, "title parse failed");
    failures += expect(strcmp(state.artist, "Artist") == 0, "artist parse failed");
    failures += expect(strcmp(state.album, "Album") == 0, "album parse failed");
    failures += expect(strcmp(state.genre, "Genre") == 0, "genre parse failed");
    failures += expect(state.active && !state.playing, "boolean parse failed");
    failures += expect(state.volume_percent == 50, "volume parse failed");
    failures += expect(state.progress_start_frame == 1000, "progress start parse failed");
    failures += expect(state.progress_current_frame == 1500, "progress current parse failed");
    failures += expect(state.progress_end_frame == 2000, "progress end parse failed");
    failures += expect(state.revision == 0, "pure parser should not bump revision");

    return failures == 0 ? 0 : 1;
}
