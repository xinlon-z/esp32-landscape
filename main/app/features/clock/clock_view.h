#pragma once

#include "app/services/clock_background_service.h"
#include "clock_model.h"
#include "lvgl.h"

class ClockView {
public:
    void create();
    void destroy();
    lv_color_t* backgroundPixels();
    void setPalette(const ClockForegroundPalette& palette);
    void showBackground(const lv_img_dsc_t& image);
    void hideBackground();
    void renderTime(const ClockDisplayState& state, bool dimmed);
    void renderBattery(const BatteryDisplayState& state, bool dimmed);
    void renderNetwork(const NetworkDisplayState& state, bool dimmed);

private:
    lv_obj_t* background_img_ = nullptr;
    lv_img_dsc_t background_image_{};
    lv_color_t* background_pixels_ = nullptr;
    ClockForegroundPalette palette_{};
    lv_obj_t* time_label_ = nullptr;
    lv_obj_t* divider_ = nullptr;
    lv_obj_t* weekday_label_ = nullptr;
    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* battery_label_ = nullptr;
    lv_obj_t* wifi_icon_ = nullptr;
    lv_obj_t* sync_icon_ = nullptr;
    lv_obj_t* power_icon_ = nullptr;
    lv_obj_t* battery_shell_ = nullptr;
    lv_obj_t* battery_fill_ = nullptr;
    lv_obj_t* battery_cap_ = nullptr;
};
