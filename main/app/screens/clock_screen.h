#pragma once

#include "app/features/clock/clock_presenter.h"
#include "app/features/clock/clock_view.h"
#include "screen.h"

class ClockScreen : public app::Screen {
public:
    ClockScreen();
    void onEnter() override;
    void onExit() override;
    void onTick() override;

private:
    ClockView view_;
    ClockPresenter presenter_;
};
