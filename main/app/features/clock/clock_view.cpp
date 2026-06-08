#include "clock_view.h"

#include "clock_battery_gauge_model.h"
#include "widgets/seven_segment_widget.h"

namespace {

constexpr uint32_t kBg     = 0xf4f1e8;
constexpr uint32_t kInk    = kClockBatteryInk;
constexpr uint32_t kDimInk = kClockBatteryDimInk;
constexpr uint32_t kMuted  = 0x6f7772;
constexpr uint32_t kFaint  = 0xded7ca;
constexpr uint32_t kAccent = 0x2f705f;

constexpr int kScreenH          = 172;
constexpr int kDigitW           = 64;
constexpr int kDigitH           = 106;
constexpr int kStroke           = 9;
constexpr int kBatteryInnerW    = 38;
constexpr int kDigitY           = (kScreenH - kDigitH) / 2;
constexpr int kColonTopY        = kDigitY + 38;
constexpr int kColonBottomY     = kDigitY + 71;

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

lv_obj_t* makeBlock(lv_obj_t* parent, int x, int y,
                    int w, int h, uint32_t color, lv_opa_t opa)
{
    lv_obj_t* block = lv_obj_create(parent);
    lv_obj_set_size(block, w, h);
    lv_obj_set_pos(block, x, y);
    lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(block, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(block, 0, 0);
    lv_obj_set_style_bg_color(block, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(block, opa, 0);
    lv_obj_set_style_pad_all(block, 0, 0);
    return block;
}

void setLabelColor(lv_obj_t* label, uint32_t color)
{
    if (label) {
        lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    }
}

void setObjColor(lv_obj_t* obj, uint32_t color)
{
    if (obj) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(color), 0);
    }
}

void setBatteryGauge(lv_obj_t* fill, lv_obj_t* shell, lv_obj_t* cap, int percent, bool dimmed)
{
    const ClockBatteryGaugeState gauge = buildClockBatteryGaugeState(percent, dimmed);

    if (fill) {
        lv_bar_set_value(fill, gauge.value, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(fill, lv_color_hex(gauge.fill_color), LV_PART_INDICATOR);
    }
    setObjColor(shell, gauge.shell_color);
    setObjColor(cap, gauge.shell_color);
}

} // namespace

void ClockView::renderDigit(int slot, char c, bool dimmed)
{
    SevenSegmentWidget digit;
    digit.bind(digit_segs_[slot]);
    digit.render(c, dimmed);
}

void ClockView::renderTime(const ClockDisplayState& state, bool dimmed)
{
    renderDigit(0, state.time[0], dimmed);
    renderDigit(1, state.time[1], dimmed);
    renderDigit(2, state.time[3], dimmed);
    renderDigit(3, state.time[4], dimmed);

    const lv_opa_t colon_opa = (state.time[2] == ':') ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_obj_set_style_bg_color(colon_top_,    lv_color_hex(dimmed ? kDimInk : kInk), 0);
    lv_obj_set_style_bg_color(colon_bottom_, lv_color_hex(dimmed ? kDimInk : kInk), 0);
    lv_obj_set_style_bg_opa(colon_top_,    colon_opa, 0);
    lv_obj_set_style_bg_opa(colon_bottom_, colon_opa, 0);

    lv_label_set_text(weekday_label_, state.weekday);
    lv_label_set_text(date_label_, state.date);
}

void ClockView::renderBattery(const BatteryDisplayState& state, bool dimmed)
{
    if (state.percent < 0) {
        setBatteryGauge(battery_fill_, battery_shell_, battery_cap_, 0, dimmed);
        lv_label_set_text(battery_label_, "--%");
        return;
    }

    setBatteryGauge(battery_fill_, battery_shell_, battery_cap_, state.percent, dimmed);
    if (state.update_label) {
        lv_label_set_text_fmt(battery_label_, "%d%%", state.percent);
    }
}

void ClockView::renderNetwork(const NetworkDisplayState& state, bool)
{
    setLabelColor(wifi_icon_, state.wifi_connected ? kInk : kFaint);

    if (state.ntp_synced) {
        lv_label_set_text(sync_icon_, LV_SYMBOL_OK);
        setLabelColor(sync_icon_, kAccent);
    } else if (state.sync_in_progress) {
        lv_label_set_text(sync_icon_, LV_SYMBOL_REFRESH);
        setLabelColor(sync_icon_, kMuted);
    } else {
        lv_label_set_text(sync_icon_, LV_SYMBOL_CLOSE);
        setLabelColor(sync_icon_, kFaint);
    }

    setLabelColor(power_icon_, state.external_power ? kAccent : kFaint);
}

void ClockView::create()
{
    lv_obj_t* screen = lv_scr_act();
    lv_obj_clean(screen);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    lv_obj_t* main_area = lv_obj_create(screen);
    lv_obj_set_size(main_area, 430, kScreenH);
    lv_obj_set_pos(main_area, 0, 0);
    lv_obj_clear_flag(main_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(main_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_area, 0, 0);
    lv_obj_set_style_pad_all(main_area, 0, 0);

    auto buildDigit = [&](int slot, int x) {
        lv_obj_t* digit = lv_obj_create(main_area);
        lv_obj_set_size(digit, kDigitW, kDigitH);
        lv_obj_set_pos(digit, x, kDigitY);
        lv_obj_clear_flag(digit, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(digit, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(digit, 0, 0);
        lv_obj_set_style_pad_all(digit, 0, 0);

        const int h_w   = kDigitW - 2 * kStroke;
        const int r_x   = kDigitW - kStroke;
        const int top_y = kStroke + 1;
        const int mid_y = (kDigitH - kStroke) / 2;
        const int bot_y = kDigitH - kStroke;
        const int low_y = (kDigitH / 2) + 4;
        const int v_h   = (kDigitH / 2) - kStroke - 2;

        digit_segs_[slot][0] = makeBlock(digit, kStroke, 0,     h_w,    kStroke, kInk, LV_OPA_TRANSP);
        digit_segs_[slot][1] = makeBlock(digit, 0,       top_y, kStroke, v_h,    kInk, LV_OPA_TRANSP);
        digit_segs_[slot][2] = makeBlock(digit, r_x,     top_y, kStroke, v_h,    kInk, LV_OPA_TRANSP);
        digit_segs_[slot][3] = makeBlock(digit, kStroke, mid_y, h_w,    kStroke, kInk, LV_OPA_TRANSP);
        digit_segs_[slot][4] = makeBlock(digit, 0,       low_y, kStroke, v_h,    kInk, LV_OPA_TRANSP);
        digit_segs_[slot][5] = makeBlock(digit, r_x,     low_y, kStroke, v_h,    kInk, LV_OPA_TRANSP);
        digit_segs_[slot][6] = makeBlock(digit, kStroke, bot_y, h_w,    kStroke, kInk, LV_OPA_TRANSP);
    };

    buildDigit(0,  50);
    buildDigit(1, 124);
    colon_top_    = makeBlock(main_area, 204, kColonTopY,    9, 9, kInk, LV_OPA_COVER);
    colon_bottom_ = makeBlock(main_area, 204, kColonBottomY, 9, 9, kInk, LV_OPA_COVER);
    buildDigit(2, 230);
    buildDigit(3, 304);

    lv_obj_t* divider = lv_obj_create(screen);
    lv_obj_set_size(divider, 1, 112);
    lv_obj_set_pos(divider, 420, 30);
    lv_obj_set_style_bg_color(divider, lv_color_hex(kFaint), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_70, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* side = lv_obj_create(screen);
    lv_obj_set_size(side, 220, kScreenH);
    lv_obj_set_pos(side, 420, 0);
    lv_obj_clear_flag(side, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(side, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(side, 0, 0);
    lv_obj_set_style_pad_all(side, 0, 0);

    weekday_label_ = makeLabel(side, "RTC", &lv_font_montserrat_20, kInk);
    lv_obj_set_pos(weekday_label_, 36, 24);
    lv_obj_set_size(weekday_label_, 104, 24);

    date_label_ = makeLabel(side, "--/--", &lv_font_montserrat_20, kInk);
    lv_obj_set_pos(date_label_, 36, 50);
    lv_obj_set_size(date_label_, 104, 24);

    wifi_icon_ = makeLabel(side, LV_SYMBOL_WIFI, &lv_font_montserrat_16, kFaint);
    lv_obj_set_pos(wifi_icon_, 36, 86);
    lv_obj_set_size(wifi_icon_, 24, 20);

    sync_icon_ = makeLabel(side, LV_SYMBOL_REFRESH, &lv_font_montserrat_16, kMuted);
    lv_obj_set_pos(sync_icon_, 72, 86);
    lv_obj_set_size(sync_icon_, 24, 20);

    power_icon_ = makeLabel(side, LV_SYMBOL_CHARGE, &lv_font_montserrat_16, kFaint);
    lv_obj_set_pos(power_icon_, 108, 86);
    lv_obj_set_size(power_icon_, 24, 20);

    battery_shell_ = lv_obj_create(side);
    lv_obj_set_size(battery_shell_, 46, 22);
    lv_obj_set_pos(battery_shell_, 36, 124);
    lv_obj_clear_flag(battery_shell_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(battery_shell_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery_shell_, 2, 0);
    lv_obj_set_style_border_color(battery_shell_, lv_color_hex(kMuted), 0);
    lv_obj_set_style_radius(battery_shell_, 4, 0);
    lv_obj_set_style_pad_all(battery_shell_, 0, 0);

    battery_fill_ = lv_bar_create(battery_shell_);
    lv_bar_set_range(battery_fill_, 0, 100);
    lv_bar_set_value(battery_fill_, 0, LV_ANIM_OFF);
    lv_obj_set_size(battery_fill_, kBatteryInnerW, 14);
    lv_obj_set_pos(battery_fill_, 2, 2);
    lv_obj_clear_flag(battery_fill_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(battery_fill_, lv_color_hex(kInk), 0);
    lv_obj_set_style_bg_opa(battery_fill_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(battery_fill_, lv_color_hex(kInk), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(battery_fill_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(battery_fill_, 0, 0);
    lv_obj_set_style_radius(battery_fill_, 2, 0);
    lv_obj_set_style_radius(battery_fill_, 2, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(battery_fill_, 0, 0);

    battery_cap_ = lv_obj_create(side);
    lv_obj_set_size(battery_cap_, 5, 12);
    lv_obj_set_pos(battery_cap_, 84, 129);
    lv_obj_clear_flag(battery_cap_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(battery_cap_, lv_color_hex(kMuted), 0);
    lv_obj_set_style_bg_opa(battery_cap_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(battery_cap_, 0, 0);
    lv_obj_set_style_radius(battery_cap_, 2, 0);
    lv_obj_set_style_pad_all(battery_cap_, 0, 0);

    battery_label_ = makeLabel(side, "--%", &lv_font_montserrat_16, kInk);
    lv_obj_set_pos(battery_label_, 100, 124);
    lv_obj_set_size(battery_label_, 52, 22);
    lv_obj_set_style_text_align(battery_label_, LV_TEXT_ALIGN_LEFT, 0);
}

void ClockView::destroy()
{
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 7; ++j) {
            digit_segs_[i][j] = nullptr;
        }
    }
    colon_top_ = colon_bottom_ = weekday_label_ = date_label_ = nullptr;
    battery_label_ = wifi_icon_ = sync_icon_ = power_icon_ = nullptr;
    battery_shell_ = battery_fill_ = battery_cap_ = nullptr;
}
