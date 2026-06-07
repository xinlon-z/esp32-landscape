#include "music_screen.h"

namespace {
constexpr uint32_t kFastTickMs = 100;
} // namespace

MusicScreen::MusicScreen() : presenter_(view_) {}

void MusicScreen::onEnter()
{
    view_.create();
    presenter_.start();
    if (!fast_timer_) {
        fast_timer_ = lv_timer_create(onFastTimer, kFastTickMs, this);
    }
}

void MusicScreen::onExit()
{
    if (fast_timer_) {
        lv_timer_del(fast_timer_);
        fast_timer_ = nullptr;
    }
    presenter_.stop();
    view_.destroy();
}

void MusicScreen::onTick()
{
    if (!fast_timer_) {
        presenter_.tick();
    }
}

void MusicScreen::onFastTimer(lv_timer_t* timer)
{
    auto* self = static_cast<MusicScreen*>(timer->user_data);
    if (self) {
        self->presenter_.tick();
    }
}
