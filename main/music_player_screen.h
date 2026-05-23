#pragma once

#include "lvgl.h"
#include "music_state.h"
#include "screen.h"

class MusicPlayerScreen : public Screen {
public:
    void create() override;
    void destroy() override;

private:
    static constexpr int kSpectrumBarCount = 44;

    static void onTimer(lv_timer_t* timer);

    void updateUi();
    lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color);
    lv_obj_t* makePanel(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color, lv_opa_t opa);
    lv_obj_t* makeRoundButton(lv_obj_t* parent, int x, int y, int size, bool primary);
    void makePrevIcon(lv_obj_t* parent, uint32_t color);
    void makePauseIcon(lv_obj_t* parent, uint32_t color);
    void makeNextIcon(lv_obj_t* parent, uint32_t color);
    void updateCover();
    bool updateBackgroundFromCover();
    bool decodeCoverJpeg(uint8_t* data, uint32_t size);

    MusicState state_;
    lv_obj_t*  title_    = nullptr;
    lv_obj_t*  subtitle_ = nullptr;
    lv_obj_t*  progress_ = nullptr;
    lv_obj_t*  elapsed_  = nullptr;
    lv_obj_t*  duration_ = nullptr;
    lv_obj_t*  play_pause_icon_ = nullptr;
    lv_obj_t*  spectrum_bars_[kSpectrumBarCount] = {};
    lv_obj_t*  background_img_ = nullptr;
    lv_obj_t*  cover_img_ = nullptr;
    lv_obj_t*  cover_band_ = nullptr;
    lv_obj_t*  cover_accent_ = nullptr;
    lv_timer_t* timer_   = nullptr;
    lv_img_dsc_t cover_dsc_{};
    lv_img_dsc_t background_dsc_{};
    lv_color_t* background_pixels_ = nullptr;
    lv_color_t* stale_background_pixels_ = nullptr;
    lv_color_t* cover_pixels_ = nullptr;
    lv_color_t* stale_cover_pixels_ = nullptr;
};
