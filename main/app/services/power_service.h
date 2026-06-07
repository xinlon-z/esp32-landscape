#pragma once

#include <stdint.h>

struct PowerSnapshot {
    bool external_power = false;
    int battery_percent = -1;
    bool dimmed = false;
    bool sleeping = false;
    uint32_t revision = 0;
};

class PowerService {
public:
    static PowerService& get();
    PowerSnapshot snapshot();
    void poll();

private:
    PowerService() = default;
    PowerSnapshot snapshot_{};
};
