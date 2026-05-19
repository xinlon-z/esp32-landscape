#include "clock_face_screen.h"

#include <stdio.h>

#include "clock_net.h"
#include "i2c_equipment.h"
#include "lvgl.h"
#include "power_mgr.h"

namespace {

// Color palette
constexpr uint32_t kBg     = 0xf4f1e8;
constexpr uint32_t kInk    = 0x22282b;
constexpr uint32_t kDimInk = 0x363b3d;
constexpr uint32_t kMuted  = 0x6f7772;
constexpr uint32_t kFaint  = 0xded7ca;
constexpr uint32_t kAccent = 0x2f705f;

// Layout constants
constexpr int kScreenH         = 172;
constexpr int kDigitW          = 64;
constexpr int kDigitH          = 106;
constexpr int kStroke          = 9;
constexpr int kBatteryInnerMaxW = 38;
constexpr int kDigitY          = (kScreenH - kDigitH) / 2;
constexpr int kColonTopY       = kDigitY + 38;
constexpr int kColonBottomY    = kDigitY + 71;

// Seven-segment digit map: kDigitMap[digit][segment 0..6]
constexpr bool kDigitMap[10][7] = {
    {true,  true,  true,  false, true,  true,  true },
    {false, false, true,  false, false, true,  false},
    {true,  false, true,  true,  true,  false, true },
    {true,  false, true,  true,  false, true,  true },
    {false, true,  true,  true,  false, true,  false},
    {true,  true,  false, true,  false, true,  true },
    {true,  true,  false, true,  true,  true,  true },
    {true,  false, true,  false, false, true,  false},
    {true,  true,  true,  true,  true,  true,  true },
    {true,  true,  true,  true,  false, true,  true },
};

const char* weekdayName(uint8_t week)
{
    static const char* kNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return kNames[week % 7];
}

int absInt(int v) { return v < 0 ? -v : v; }

} // namespace

// ── Helpers ─────────────────────────────────────────────────────────────────

lv_obj_t* ClockFaceScreen::makeLabel(lv_obj_t* parent, const char* text,
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

lv_obj_t* ClockFaceScreen::makeBlock(lv_obj_t* parent, int x, int y,
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

void ClockFaceScreen::setLabelColor(lv_obj_t* label, uint32_t color)
{
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

void ClockFaceScreen::setObjColor(lv_obj_t* obj, uint32_t color)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(color), 0);
}

// ── Rendering ───────────────────────────────────────────────────────────────

void ClockFaceScreen::setSegmentState(lv_obj_t* seg, bool active)
{
    lv_obj_set_style_bg_color(seg, lv_color_hex(dimmed_ ? kDimInk : kInk), 0);
    lv_obj_set_style_bg_opa(seg, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

void ClockFaceScreen::renderDigit(int slot, char c)
{
    for (int s = 0; s < 7; ++s) {
        bool active = false;
        if (c >= '0' && c <= '9') {
            active = kDigitMap[c - '0'][s];
        } else if (c == '-') {
            active = (s == 3);
        }
        setSegmentState(digit_segs_[slot][s], active);
    }
}

void ClockFaceScreen::renderTime(const char* time_text)
{
    snprintf(last_time_, sizeof(last_time_), "%s", time_text);
    renderDigit(0, time_text[0]);
    renderDigit(1, time_text[1]);
    renderDigit(2, time_text[3]);
    renderDigit(3, time_text[4]);

    const lv_opa_t colon_opa = (time_text[2] == ':') ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_obj_set_style_bg_color(colon_top_,    lv_color_hex(dimmed_ ? kDimInk : kInk), 0);
    lv_obj_set_style_bg_color(colon_bottom_,  lv_color_hex(dimmed_ ? kDimInk : kInk), 0);
    lv_obj_set_style_bg_opa(colon_top_,    colon_opa, 0);
    lv_obj_set_style_bg_opa(colon_bottom_, colon_opa, 0);
}

void ClockFaceScreen::setBatteryGauge(int percent)
{
    const int      fill_w     = kBatteryInnerMaxW * percent / 100;
    const uint32_t fill_color = (percent <= 15) ? 0xa24535u : (dimmed_ ? kDimInk : kInk);
    const uint32_t shell_color = dimmed_ ? kDimInk : kInk;

    lv_obj_set_width(battery_fill_, fill_w);
    setObjColor(battery_fill_,  fill_color);
    setObjColor(battery_shell_, shell_color);
    setObjColor(battery_cap_,   shell_color);
}

void ClockFaceScreen::updateBatteryUi(int percent)
{
    if (percent < 0) {
        setBatteryGauge(0);
        lv_label_set_text(battery_label_, "--%");
        battery_disp_pct_ = -1;
        return;
    }
    if (battery_disp_pct_ < 0 || absInt(percent - battery_disp_pct_) >= 5) {
        battery_disp_pct_ = percent;
        setBatteryGauge(percent);
        lv_label_set_text_fmt(battery_label_, "%d%%", percent);
    }
}

void ClockFaceScreen::updateNetworkIcons()
{
    const ClockNet::Status net = ClockNet::getStatus();

    setLabelColor(wifi_icon_, net.wifi_connected ? kInk : kFaint);

    if (net.ntp_synced) {
        lv_label_set_text(sync_icon_, LV_SYMBOL_OK);
        setLabelColor(sync_icon_, kAccent);
    } else if (net.sync_in_progress) {
        lv_label_set_text(sync_icon_, LV_SYMBOL_REFRESH);
        setLabelColor(sync_icon_, kMuted);
    } else {
        lv_label_set_text(sync_icon_, LV_SYMBOL_CLOSE);
        setLabelColor(sync_icon_, kFaint);
    }

    setLabelColor(power_icon_, external_power_ ? kAccent : kFaint);
}

// ── Timer callback ───────────────────────────────────────────────────────────

void ClockFaceScreen::onTimer(lv_timer_t* t)
{
    static_cast<ClockFaceScreen*>(t->user_data)->updateCb();
}

void ClockFaceScreen::updateCb()
{
    // Poll power state and react to transitions.
    const PowerManager::State pwr = PowerManager::getState();
    if (pwr.dimmed != dimmed_) {
        dimmed_ = pwr.dimmed;
        renderTime(last_time_);
        if (battery_disp_pct_ >= 0) setBatteryGauge(battery_disp_pct_);
    }
    if (pwr.external_power != external_power_) {
        external_power_ = pwr.external_power;
    }

    // Update time from RTC.
    const RtcDateTime_t now = i2c_rtc_get();
    const bool rtc_ok = !(now.hour > 23 || now.minute > 59 ||
                          now.month == 0 || now.month > 12 ||
                          now.day == 0   || now.day > 31);

    if (!rtc_ok) {
        renderTime("--:--");
        lv_label_set_text(weekday_label_, "RTC");
        lv_label_set_text(date_label_,    "--/--");
    } else {
        char time_text[6];
        time_text[0] = static_cast<char>('0' + (now.hour   / 10));
        time_text[1] = static_cast<char>('0' + (now.hour   % 10));
        time_text[2] = (now.second % 2 == 0) ? ':' : ' ';
        time_text[3] = static_cast<char>('0' + (now.minute / 10));
        time_text[4] = static_cast<char>('0' + (now.minute % 10));
        time_text[5] = '\0';
        renderTime(time_text);
        lv_label_set_text(weekday_label_, weekdayName(now.week));
        lv_label_set_text_fmt(date_label_, "%02u/%02u", now.month, now.day);
    }

    updateNetworkIcons();
    updateBatteryUi(pwr.battery_percent);
}

// ── Digit widget builder ─────────────────────────────────────────────────────

void ClockFaceScreen::create()
{
    lv_obj_t* screen = lv_scr_act();
    lv_obj_clean(screen);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    // Main area (clock digits + colon)
    lv_obj_t* main_area = lv_obj_create(screen);
    lv_obj_set_size(main_area, 430, kScreenH);
    lv_obj_set_pos(main_area, 0, 0);
    lv_obj_clear_flag(main_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(main_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_area, 0, 0);
    lv_obj_set_style_pad_all(main_area, 0, 0);

    // Build four digit widgets
    auto buildDigit = [&](int slot, int x) {
        lv_obj_t* digit = lv_obj_create(main_area);
        lv_obj_set_size(digit, kDigitW, kDigitH);
        lv_obj_set_pos(digit, x, kDigitY);
        lv_obj_clear_flag(digit, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(digit, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(digit, 0, 0);
        lv_obj_set_style_pad_all(digit, 0, 0);

        const int h_w    = kDigitW - 2 * kStroke;
        const int r_x    = kDigitW - kStroke;
        const int top_y  = kStroke + 1;
        const int mid_y  = (kDigitH - kStroke) / 2;
        const int bot_y  = kDigitH - kStroke;
        const int low_y  = (kDigitH / 2) + 4;
        const int v_h    = (kDigitH / 2) - kStroke - 2;

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

    // Divider
    lv_obj_t* divider = lv_obj_create(screen);
    lv_obj_set_size(divider, 1, 112);
    lv_obj_set_pos(divider, 420, 30);
    lv_obj_set_style_bg_color(divider, lv_color_hex(kFaint), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_70, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);

    // Side panel (date, icons, battery)
    lv_obj_t* side = lv_obj_create(screen);
    lv_obj_set_size(side, 220, kScreenH);
    lv_obj_set_pos(side, 420, 0);
    lv_obj_clear_flag(side, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(side, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(side, 0, 0);
    lv_obj_set_style_pad_all(side, 0, 0);

    weekday_label_ = makeLabel(side, "RTC",   &lv_font_montserrat_20, kInk);
    lv_obj_set_pos(weekday_label_, 36, 24);
    lv_obj_set_size(weekday_label_, 104, 24);

    date_label_ = makeLabel(side, "--/--", &lv_font_montserrat_20, kInk);
    lv_obj_set_pos(date_label_, 36, 50);
    lv_obj_set_size(date_label_, 104, 24);

    wifi_icon_ = makeLabel(side, LV_SYMBOL_WIFI,   &lv_font_montserrat_16, kFaint);
    lv_obj_set_pos(wifi_icon_, 36, 86);
    lv_obj_set_size(wifi_icon_, 24, 20);

    sync_icon_ = makeLabel(side, LV_SYMBOL_REFRESH, &lv_font_montserrat_16, kMuted);
    lv_obj_set_pos(sync_icon_, 72, 86);
    lv_obj_set_size(sync_icon_, 24, 20);

    power_icon_ = makeLabel(side, LV_SYMBOL_CHARGE, &lv_font_montserrat_16, kFaint);
    lv_obj_set_pos(power_icon_, 108, 86);
    lv_obj_set_size(power_icon_, 24, 20);

    // Battery shell
    battery_shell_ = lv_obj_create(side);
    lv_obj_set_size(battery_shell_, 46, 22);
    lv_obj_set_pos(battery_shell_, 36, 124);
    lv_obj_clear_flag(battery_shell_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(battery_shell_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery_shell_, 2, 0);
    lv_obj_set_style_border_color(battery_shell_, lv_color_hex(kMuted), 0);
    lv_obj_set_style_radius(battery_shell_, 4, 0);
    lv_obj_set_style_pad_all(battery_shell_, 0, 0);

    battery_fill_ = lv_obj_create(battery_shell_);
    lv_obj_set_size(battery_fill_, 0, 14);
    lv_obj_set_pos(battery_fill_, 2, 2);
    lv_obj_clear_flag(battery_fill_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(battery_fill_, lv_color_hex(kInk), 0);
    lv_obj_set_style_bg_opa(battery_fill_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(battery_fill_, 0, 0);
    lv_obj_set_style_radius(battery_fill_, 2, 0);
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

    // 1-second update timer; passes 'this' as user_data for instance dispatch.
    timer_ = lv_timer_create(onTimer, 1000, this);
    updateCb();
}

void ClockFaceScreen::destroy()
{
    if (timer_) {
        lv_timer_del(timer_);
        timer_ = nullptr;
    }
    // Zero all handles so a subsequent create() starts clean.
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 7; ++j)
            digit_segs_[i][j] = nullptr;
    colon_top_ = colon_bottom_ = weekday_label_ = date_label_   = nullptr;
    battery_label_ = wifi_icon_ = sync_icon_ = power_icon_       = nullptr;
    battery_shell_ = battery_fill_ = battery_cap_                = nullptr;
    battery_disp_pct_ = -1;
    dimmed_ = external_power_ = false;
}
