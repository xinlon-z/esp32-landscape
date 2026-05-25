#pragma once

#include "clock_model.h"
#include "clock_view.h"

#include <stdint.h>

class ClockPresenter {
public:
    explicit ClockPresenter(ClockView& view);
    void start();
    void stop();
    void tick();

private:
    ClockView& view_;
    ClockModel model_;
    bool running_ = false;
    bool dimmed_ = false;
    bool external_power_ = false;
    uint32_t last_time_revision_ = 0;
    uint32_t last_power_revision_ = 0;
    uint32_t last_network_revision_ = 0;

    void renderAll();
};
