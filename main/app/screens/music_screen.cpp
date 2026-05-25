#include "music_screen.h"

MusicScreen::MusicScreen() : presenter_(view_) {}

void MusicScreen::onEnter()
{
    view_.create();
    presenter_.start();
}

void MusicScreen::onExit()
{
    presenter_.stop();
    view_.destroy();
}

void MusicScreen::onTick()
{
    presenter_.tick();
}
