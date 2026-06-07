#include "clock_screen.h"

ClockScreen::ClockScreen() : presenter_(view_) {}

void ClockScreen::onEnter()
{
    view_.create();
    presenter_.start();
}

void ClockScreen::onExit()
{
    presenter_.stop();
    view_.destroy();
}

void ClockScreen::onTick()
{
    presenter_.tick();
}
