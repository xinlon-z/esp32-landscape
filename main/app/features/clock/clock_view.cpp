#include "clock_view.h"

#include "clock_battery_gauge_model.h"
#include "app/ui/fonts/clock_fonts.h"
#include "esp_heap_caps.h"

namespace {

constexpr uint32_t kBg     = 0xf4f1e8;
constexpr int kScreenW          = ClockBackgroundService::kWidth;
constexpr int kScreenH          = 172;
constexpr int kBatteryInnerW    = 38;
constexpr int kTimeAreaW        = 420;
constexpr lv_opa_t kTimeTextOpa = LV_OPA_COVER;
constexpr lv_opa_t kTimeTextDimOpa = LV_OPA_COVER;

lv_obj_t* makeLabel(lv_obj_t* parent, const char* text,
                    const lv_font_t* font, uint32_t color)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    return label;
}

void setLabelColor(lv_obj_t* label, uint32_t color)
{
    if (label) {
        lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    }
}

void setLabelOpacity(lv_obj_t* label, lv_opa_t opa)
{
    if (label) {
        lv_obj_set_style_text_opa(label, opa, 0);
    }
}

void setObjColor(lv_obj_t* obj, uint32_t color)
{
    if (obj) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(color), 0);
    }
}

uint32_t textColor(const ClockForegroundPalette& palette, bool dimmed)
{
    return dimmed ? palette.dim : palette.fg;
}

int timeLabelY(const ClockFontVisualMetrics& metrics)
{
    if (metrics.line_height <= 0 || metrics.digit_bottom <= metrics.digit_top) {
        return 0;
    }
    return (kScreenH - metrics.digit_top - metrics.digit_bottom) / 2;
}

int timeLabelHeight(const ClockFontVisualMetrics& metrics)
{
    return metrics.line_height > 0 ? metrics.line_height : kScreenH;
}

void setBatteryGauge(lv_obj_t* fill, lv_obj_t* shell, lv_obj_t* cap,
                     int percent, bool dimmed,
                     const ClockForegroundPalette& palette)
{
    const ClockBatteryGaugeState gauge = buildClockBatteryGaugeState(percent, dimmed);
    const uint32_t shell_color = dimmed ? palette.dim : palette.fg;
    const uint32_t fill_color = (gauge.value > 0 && gauge.value <= 15)
        ? kClockBatteryLow
        : shell_color;

    if (fill) {
        lv_bar_set_value(fill, gauge.value, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(fill, lv_color_hex(fill_color), LV_PART_INDICATOR);
    }
    setObjColor(shell, shell_color);
    setObjColor(cap, shell_color);
}

} // namespace

lv_color_t* ClockView::backgroundPixels()
{
    return background_pixels_;
}

void ClockView::setPalette(const ClockForegroundPalette& palette)
{
    palette_ = palette;
    if (divider_) {
        lv_obj_set_style_bg_color(divider_, lv_color_hex(palette_.fg), 0);
    }
}

void ClockView::showBackground(const lv_img_dsc_t& image)
{
    if (!background_img_ || !background_pixels_) {
        return;
    }

    background_image_ = image;
    background_image_.data = reinterpret_cast<const uint8_t*>(background_pixels_);
    background_image_.data_size = ClockBackgroundService::kPixelCount * sizeof(lv_color_t);
    lv_img_cache_invalidate_src(&background_image_);
    lv_img_set_src(background_img_, &background_image_);
    lv_obj_clear_flag(background_img_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(background_img_);
    lv_obj_invalidate(background_img_);
}

void ClockView::hideBackground()
{
    if (background_img_) {
        lv_obj_add_flag(background_img_, LV_OBJ_FLAG_HIDDEN);
    }
}

void ClockView::renderTime(const ClockDisplayState& state, bool dimmed)
{
    const uint32_t label_color = textColor(palette_, dimmed);
    const lv_opa_t label_opa = dimmed ? kTimeTextDimOpa : kTimeTextOpa;
    setLabelColor(time_label_, label_color);
    setLabelOpacity(time_label_, label_opa);
    lv_label_set_text(time_label_, state.time);
    if (divider_) {
        lv_obj_set_style_bg_color(divider_, lv_color_hex(label_color), 0);
    }

    setLabelColor(weekday_label_, label_color);
    setLabelColor(date_label_, label_color);
    lv_label_set_text(weekday_label_, state.weekday);
    lv_label_set_text(date_label_, state.date);
}

void ClockView::renderBattery(const BatteryDisplayState& state, bool dimmed)
{
    setLabelColor(battery_label_, textColor(palette_, dimmed));
    if (state.percent < 0) {
        setBatteryGauge(battery_fill_, battery_shell_, battery_cap_, 0, dimmed, palette_);
        lv_label_set_text(battery_label_, "--%");
        return;
    }

    setBatteryGauge(battery_fill_, battery_shell_, battery_cap_,
                    state.percent, dimmed, palette_);
    if (state.update_label) {
        lv_label_set_text_fmt(battery_label_, "%d%%", state.percent);
    }
}

void ClockView::renderNetwork(const NetworkDisplayState& state, bool)
{
    setLabelColor(wifi_icon_, state.wifi_connected ? palette_.fg : palette_.faint);

    if (state.ntp_synced) {
        lv_label_set_text(sync_icon_, LV_SYMBOL_OK);
        setLabelColor(sync_icon_, palette_.accent);
    } else if (state.sync_in_progress) {
        lv_label_set_text(sync_icon_, LV_SYMBOL_REFRESH);
        setLabelColor(sync_icon_, palette_.muted);
    } else {
        lv_label_set_text(sync_icon_, LV_SYMBOL_CLOSE);
        setLabelColor(sync_icon_, palette_.faint);
    }

    setLabelColor(power_icon_, state.external_power ? palette_.accent : palette_.faint);
}

void ClockView::create()
{
    lv_obj_t* screen = lv_scr_act();
    lv_obj_clean(screen);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    background_pixels_ = static_cast<lv_color_t*>(
        heap_caps_malloc(ClockBackgroundService::kPixelCount * sizeof(lv_color_t),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!background_pixels_) {
        background_pixels_ = static_cast<lv_color_t*>(
            heap_caps_malloc(ClockBackgroundService::kPixelCount * sizeof(lv_color_t),
                             MALLOC_CAP_8BIT));
    }

    background_img_ = lv_img_create(screen);
    lv_obj_set_size(background_img_, kScreenW, kScreenH);
    lv_obj_set_pos(background_img_, 0, 0);
    lv_obj_set_style_radius(background_img_, 0, 0);
    lv_obj_set_style_clip_corner(background_img_, false, 0);
    lv_obj_add_flag(background_img_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* main_area = lv_obj_create(screen);
    lv_obj_set_size(main_area, kTimeAreaW, kScreenH);
    lv_obj_set_pos(main_area, 0, 0);
    lv_obj_clear_flag(main_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(main_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_area, 0, 0);
    lv_obj_set_style_pad_all(main_area, 0, 0);

    const lv_font_t* time_font = clockTimeFont();
    const ClockFontVisualMetrics time_metrics = clockTimeFontMetrics();
    const int time_label_h = timeLabelHeight(time_metrics);
    time_label_ = makeLabel(main_area, "--:--", time_font, palette_.fg);
    lv_label_set_long_mode(time_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(time_label_, 0, timeLabelY(time_metrics));
    lv_obj_set_size(time_label_, kTimeAreaW, time_label_h);
    lv_obj_set_style_text_align(time_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_opa(time_label_, kTimeTextOpa, 0);

    divider_ = lv_obj_create(screen);
    lv_obj_set_size(divider_, 1, 112);
    lv_obj_set_pos(divider_, 420, 30);
    lv_obj_set_style_bg_color(divider_, lv_color_hex(palette_.faint), 0);
    lv_obj_set_style_bg_opa(divider_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider_, 0, 0);
    lv_obj_clear_flag(divider_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* side = lv_obj_create(screen);
    lv_obj_set_size(side, 220, kScreenH);
    lv_obj_set_pos(side, 420, 0);
    lv_obj_clear_flag(side, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(side, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(side, 0, 0);
    lv_obj_set_style_pad_all(side, 0, 0);

    weekday_label_ = makeLabel(side, "RTC", &lv_font_montserrat_20, palette_.fg);
    lv_obj_set_pos(weekday_label_, 36, 24);
    lv_obj_set_size(weekday_label_, 104, 24);

    date_label_ = makeLabel(side, "--/--", &lv_font_montserrat_20, palette_.fg);
    lv_obj_set_pos(date_label_, 36, 50);
    lv_obj_set_size(date_label_, 104, 24);

    wifi_icon_ = makeLabel(side, LV_SYMBOL_WIFI, &lv_font_montserrat_16, palette_.faint);
    lv_obj_set_pos(wifi_icon_, 36, 86);
    lv_obj_set_size(wifi_icon_, 24, 20);

    sync_icon_ = makeLabel(side, LV_SYMBOL_REFRESH, &lv_font_montserrat_16, palette_.muted);
    lv_obj_set_pos(sync_icon_, 72, 86);
    lv_obj_set_size(sync_icon_, 24, 20);

    power_icon_ = makeLabel(side, LV_SYMBOL_CHARGE, &lv_font_montserrat_16, palette_.faint);
    lv_obj_set_pos(power_icon_, 108, 86);
    lv_obj_set_size(power_icon_, 24, 20);

    battery_shell_ = lv_obj_create(side);
    lv_obj_set_size(battery_shell_, 46, 22);
    lv_obj_set_pos(battery_shell_, 36, 124);
    lv_obj_clear_flag(battery_shell_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(battery_shell_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery_shell_, 2, 0);
    lv_obj_set_style_border_color(battery_shell_, lv_color_hex(palette_.muted), 0);
    lv_obj_set_style_radius(battery_shell_, 4, 0);
    lv_obj_set_style_pad_all(battery_shell_, 0, 0);

    battery_fill_ = lv_bar_create(battery_shell_);
    lv_bar_set_range(battery_fill_, 0, 100);
    lv_bar_set_value(battery_fill_, 0, LV_ANIM_OFF);
    lv_obj_set_size(battery_fill_, kBatteryInnerW, 14);
    lv_obj_set_pos(battery_fill_, 2, 2);
    lv_obj_clear_flag(battery_fill_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(battery_fill_, lv_color_hex(palette_.fg), 0);
    lv_obj_set_style_bg_opa(battery_fill_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(battery_fill_, lv_color_hex(palette_.fg), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(battery_fill_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(battery_fill_, 0, 0);
    lv_obj_set_style_radius(battery_fill_, 2, 0);
    lv_obj_set_style_radius(battery_fill_, 2, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(battery_fill_, 0, 0);

    battery_cap_ = lv_obj_create(side);
    lv_obj_set_size(battery_cap_, 5, 12);
    lv_obj_set_pos(battery_cap_, 84, 129);
    lv_obj_clear_flag(battery_cap_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(battery_cap_, lv_color_hex(palette_.muted), 0);
    lv_obj_set_style_bg_opa(battery_cap_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(battery_cap_, 0, 0);
    lv_obj_set_style_radius(battery_cap_, 2, 0);
    lv_obj_set_style_pad_all(battery_cap_, 0, 0);

    battery_label_ = makeLabel(side, "--%", &lv_font_montserrat_16, palette_.fg);
    lv_obj_set_pos(battery_label_, 100, 124);
    lv_obj_set_size(battery_label_, 52, 22);
    lv_obj_set_style_text_align(battery_label_, LV_TEXT_ALIGN_LEFT, 0);
}

void ClockView::destroy()
{
    if (background_pixels_) {
        heap_caps_free(background_pixels_);
        background_pixels_ = nullptr;
    }
    background_img_ = nullptr;
    background_image_ = {};
    time_label_ = nullptr;
    divider_ = nullptr;
    weekday_label_ = date_label_ = nullptr;
    battery_label_ = wifi_icon_ = sync_icon_ = power_icon_ = nullptr;
    battery_shell_ = battery_fill_ = battery_cap_ = nullptr;
}
