#include "music_screen.h"

void MusicScreen::onEnter()
{
    legacy_.create();
}

void MusicScreen::onExit()
{
    legacy_.destroy();
}

void MusicScreen::onTick()
{
}
