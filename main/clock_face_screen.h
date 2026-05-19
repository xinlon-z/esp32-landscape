#pragma once

#include "lvgl.h"
#include "screen.h"

// Landscape seven-segment clock face: time, date, weekday, battery gauge,
// and network/power status icons.
//
// Both create() and destroy() must be called with the LVGL lock held
// (e.g. inside a LvglPort::Guard block).
class ClockFaceScreen : public Screen {
public:
    void create()  override;
    void destroy() override;

private:
    // LVGL timer callback — dispatches to the owning ClockFaceScreen instance
    // via t->user_data so multiple instances can coexist without static state.
    static void onTimer(lv_timer_t* t);

    void updateCb();
    void renderTime(const char* time_text);
    void renderDigit(int slot, char c);
    void setSegmentState(lv_obj_t* seg, bool active);
    void setBatteryGauge(int percent);
    void updateBatteryUi(int percent);
    void updateNetworkIcons();

    lv_obj_t* makeLabel(lv_obj_t* parent, const char* text,
                        const lv_font_t* font, uint32_t color);
    lv_obj_t* makeBlock(lv_obj_t* parent, int x, int y,
                        int w, int h, uint32_t color, lv_opa_t opa);
    void setLabelColor(lv_obj_t* label, uint32_t color);
    void setObjColor(lv_obj_t* obj, uint32_t color);

    // LVGL widget handles
    lv_obj_t*   digit_segs_[4][7] = {};
    lv_obj_t*   colon_top_        = nullptr;
    lv_obj_t*   colon_bottom_     = nullptr;
    lv_obj_t*   weekday_label_    = nullptr;
    lv_obj_t*   date_label_       = nullptr;
    lv_obj_t*   battery_label_    = nullptr;
    lv_obj_t*   wifi_icon_        = nullptr;
    lv_obj_t*   sync_icon_        = nullptr;
    lv_obj_t*   power_icon_       = nullptr;
    lv_obj_t*   battery_shell_    = nullptr;
    lv_obj_t*   battery_fill_     = nullptr;
    lv_obj_t*   battery_cap_      = nullptr;
    lv_timer_t* timer_            = nullptr;

    // Render state (cached to detect changes from PowerManager::getState())
    bool dimmed_           = false;
    bool external_power_   = false;
    char last_time_[6]     = "--:--";
    int  battery_disp_pct_ = -1;
};
