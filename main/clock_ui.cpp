#include "clock_ui.h"

#include <stdio.h>

#include "adc_bsp.h"
#include "clock_net.h"
#include "i2c_equipment.h"

namespace {

constexpr uint32_t kBg = 0xf4f1e8;
constexpr uint32_t kInk = 0x22282b;
constexpr uint32_t kDimInk = 0x363b3d;
constexpr uint32_t kMuted = 0x6f7772;
constexpr uint32_t kFaint = 0xded7ca;
constexpr uint32_t kAccent = 0x2f705f;

constexpr int kScreenH = 172;
constexpr int kDigitW = 64;
constexpr int kDigitH = 106;
constexpr int kStroke = 9;
constexpr int kBatteryInnerMaxW = 38;
constexpr int kDigitY = (kScreenH - kDigitH) / 2;
constexpr int kColonTopY = kDigitY + 38;
constexpr int kColonBottomY = kDigitY + 71;

constexpr bool kDigitMap[10][7] = {
    {true, true, true, false, true, true, true},
    {false, false, true, false, false, true, false},
    {true, false, true, true, true, false, true},
    {true, false, true, true, false, true, true},
    {false, true, true, true, false, true, false},
    {true, true, false, true, false, true, true},
    {true, true, false, true, true, true, true},
    {true, false, true, false, false, true, false},
    {true, true, true, true, true, true, true},
    {true, true, true, true, false, true, true},
};

lv_obj_t *s_digit_segments[4][7] = {};
lv_obj_t *s_colon_top = nullptr;
lv_obj_t *s_colon_bottom = nullptr;
lv_obj_t *s_weekday_label = nullptr;
lv_obj_t *s_date_label = nullptr;
lv_obj_t *s_battery_label = nullptr;
lv_obj_t *s_wifi_icon = nullptr;
lv_obj_t *s_sync_icon = nullptr;
lv_obj_t *s_power_icon = nullptr;
lv_obj_t *s_battery_shell = nullptr;
lv_obj_t *s_battery_fill = nullptr;
lv_obj_t *s_battery_cap = nullptr;
bool s_dimmed = false;
bool s_rtc_ok = false;
bool s_external_power = false;
char s_last_time[6] = "--:--";
int s_battery_display_percent = -1;
float s_battery_filtered_percent = -1.0f;
uint8_t s_battery_tick = 0;

const char *weekday_name(uint8_t week)
{
    static const char *kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return kWeekdays[week % 7];
}

int voltage_to_percent(float voltage)
{
    if (voltage <= 3.30f) {
        return 0;
    }
    if (voltage >= 4.20f) {
        return 100;
    }
    return static_cast<int>(((voltage - 3.30f) * 100.0f / 0.90f) + 0.5f);
}

int clamp_percent(int percent)
{
    if (percent < 0) {
        return 0;
    }
    if (percent > 100) {
        return 100;
    }
    return percent;
}

int abs_int(int value)
{
    return value < 0 ? -value : value;
}

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    return label;
}

void set_label_color(lv_obj_t *label, uint32_t color)
{
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

lv_obj_t *make_block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, lv_opa_t opa)
{
    lv_obj_t *block = lv_obj_create(parent);
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

void set_segment_state(lv_obj_t *segment, bool active)
{
    lv_obj_set_style_bg_color(segment, lv_color_hex(s_dimmed ? kDimInk : kInk), 0);
    lv_obj_set_style_bg_opa(segment, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

void create_digit(lv_obj_t *parent, int slot, int x)
{
    lv_obj_t *digit = lv_obj_create(parent);
    lv_obj_set_size(digit, kDigitW, kDigitH);
    lv_obj_set_pos(digit, x, kDigitY);
    lv_obj_clear_flag(digit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(digit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(digit, 0, 0);
    lv_obj_set_style_pad_all(digit, 0, 0);

    const int h_w = kDigitW - (2 * kStroke);
    const int r_x = kDigitW - kStroke;
    const int top_y = kStroke + 1;
    const int mid_y = (kDigitH - kStroke) / 2;
    const int bottom_y = kDigitH - kStroke;
    const int lower_y = (kDigitH / 2) + 4;
    const int v_h = (kDigitH / 2) - kStroke - 2;

    s_digit_segments[slot][0] = make_block(digit, kStroke, 0, h_w, kStroke, kInk, LV_OPA_TRANSP);
    s_digit_segments[slot][1] = make_block(digit, 0, top_y, kStroke, v_h, kInk, LV_OPA_TRANSP);
    s_digit_segments[slot][2] = make_block(digit, r_x, top_y, kStroke, v_h, kInk, LV_OPA_TRANSP);
    s_digit_segments[slot][3] = make_block(digit, kStroke, mid_y, h_w, kStroke, kInk, LV_OPA_TRANSP);
    s_digit_segments[slot][4] = make_block(digit, 0, lower_y, kStroke, v_h, kInk, LV_OPA_TRANSP);
    s_digit_segments[slot][5] = make_block(digit, r_x, lower_y, kStroke, v_h, kInk, LV_OPA_TRANSP);
    s_digit_segments[slot][6] = make_block(digit, kStroke, bottom_y, h_w, kStroke, kInk, LV_OPA_TRANSP);
}

void render_digit(int slot, char c)
{
    for (int segment = 0; segment < 7; ++segment) {
        bool active = false;
        if (c >= '0' && c <= '9') {
            active = kDigitMap[c - '0'][segment];
        } else if (c == '-') {
            active = segment == 3;
        }
        set_segment_state(s_digit_segments[slot][segment], active);
    }
}

void render_time(const char *time_text)
{
    snprintf(s_last_time, sizeof(s_last_time), "%s", time_text);
    render_digit(0, time_text[0]);
    render_digit(1, time_text[1]);
    render_digit(2, time_text[3]);
    render_digit(3, time_text[4]);

    const lv_opa_t colon_opa = time_text[2] == ':' ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_obj_set_style_bg_color(s_colon_top, lv_color_hex(s_dimmed ? kDimInk : kInk), 0);
    lv_obj_set_style_bg_color(s_colon_bottom, lv_color_hex(s_dimmed ? kDimInk : kInk), 0);
    lv_obj_set_style_bg_opa(s_colon_top, colon_opa, 0);
    lv_obj_set_style_bg_opa(s_colon_bottom, colon_opa, 0);
}

void set_obj_color(lv_obj_t *obj, uint32_t color)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(color), 0);
}

bool read_battery_percent(int *percent, float *voltage_out)
{
    float voltage_sum = 0.0f;
    int valid = 0;

    for (int i = 0; i < 8; ++i) {
        float voltage = 0.0f;
        int raw = 0;
        adc_get_value(&voltage, &raw);
        if (voltage > 0.1f) {
            voltage_sum += voltage;
            ++valid;
        }
    }

    if (valid == 0) {
        return false;
    }

    float voltage = voltage_sum / valid;
    int measured_percent = clamp_percent(voltage_to_percent(voltage));
    if (s_battery_filtered_percent < 0.0f) {
        s_battery_filtered_percent = measured_percent;
    } else {
        s_battery_filtered_percent = (s_battery_filtered_percent * 0.93f) + (measured_percent * 0.07f);
    }

    *percent = clamp_percent(static_cast<int>(s_battery_filtered_percent + 0.5f));
    *voltage_out = voltage;
    return true;
}

void set_battery_gauge(int percent)
{
    const int fill_w = kBatteryInnerMaxW * percent / 100;
    const uint32_t color = percent <= 15 ? 0xa24535 : (s_dimmed ? kDimInk : kInk);
    const uint32_t shell_color = s_dimmed ? kDimInk : kInk;

    lv_obj_set_width(s_battery_fill, fill_w);
    set_obj_color(s_battery_fill, color);
    set_obj_color(s_battery_shell, shell_color);
    set_obj_color(s_battery_cap, shell_color);
}

void update_network_icons()
{
    clock_net_status_t net = clock_net_get_status();

    set_label_color(s_wifi_icon, net.wifi_connected ? kInk : kFaint);
    if (net.ntp_synced) {
        lv_label_set_text(s_sync_icon, LV_SYMBOL_OK);
        set_label_color(s_sync_icon, kAccent);
    } else if (net.sync_in_progress) {
        lv_label_set_text(s_sync_icon, LV_SYMBOL_REFRESH);
        set_label_color(s_sync_icon, kMuted);
    } else {
        lv_label_set_text(s_sync_icon, LV_SYMBOL_CLOSE);
        set_label_color(s_sync_icon, kFaint);
    }

    set_label_color(s_power_icon, s_external_power ? kAccent : kFaint);
}

void update_battery_ui(bool force)
{
    if (!force && ++s_battery_tick < 5) {
        return;
    }
    s_battery_tick = 0;

    int percent = 0;
    float voltage = 0.0f;
    if (!read_battery_percent(&percent, &voltage)) {
        set_battery_gauge(0);
        lv_label_set_text(s_battery_label, "--%");
        return;
    }

    if (force || s_battery_display_percent < 0 || abs_int(percent - s_battery_display_percent) >= 5) {
        s_battery_display_percent = percent;
        set_battery_gauge(percent);
        lv_label_set_text_fmt(s_battery_label, "%d%%", percent);
    }
}

void update_cb(lv_timer_t *)
{
    RtcDateTime_t now = i2c_rtc_get();

    s_rtc_ok = !(now.hour > 23 || now.minute > 59 || now.month == 0 || now.month > 12 || now.day == 0 || now.day > 31);
    if (!s_rtc_ok) {
        render_time("--:--");
        lv_label_set_text(s_weekday_label, "RTC");
        lv_label_set_text(s_date_label, "--/--");
    } else {
        char time_text[6];
        time_text[0] = static_cast<char>('0' + (now.hour / 10));
        time_text[1] = static_cast<char>('0' + (now.hour % 10));
        time_text[2] = (now.second % 2 == 0) ? ':' : ' ';
        time_text[3] = static_cast<char>('0' + (now.minute / 10));
        time_text[4] = static_cast<char>('0' + (now.minute % 10));
        time_text[5] = '\0';
        render_time(time_text);
        lv_label_set_text(s_weekday_label, weekday_name(now.week));
        lv_label_set_text_fmt(s_date_label, "%02u/%02u", now.month, now.day);
    }

    update_network_icons();
    update_battery_ui(false);
}

} // namespace

extern "C" void clock_ui_create(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_clean(screen);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    lv_obj_t *main_area = lv_obj_create(screen);
    lv_obj_set_size(main_area, 430, kScreenH);
    lv_obj_set_pos(main_area, 0, 0);
    lv_obj_clear_flag(main_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(main_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_area, 0, 0);
    lv_obj_set_style_pad_all(main_area, 0, 0);

    create_digit(main_area, 0, 50);
    create_digit(main_area, 1, 124);
    s_colon_top = make_block(main_area, 204, kColonTopY, 9, 9, kInk, LV_OPA_COVER);
    s_colon_bottom = make_block(main_area, 204, kColonBottomY, 9, 9, kInk, LV_OPA_COVER);
    create_digit(main_area, 2, 230);
    create_digit(main_area, 3, 304);

    lv_obj_t *divider = lv_obj_create(screen);
    lv_obj_set_size(divider, 1, 112);
    lv_obj_set_pos(divider, 420, 30);
    lv_obj_set_style_bg_color(divider, lv_color_hex(kFaint), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_70, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *side = lv_obj_create(screen);
    lv_obj_set_size(side, 220, kScreenH);
    lv_obj_set_pos(side, 420, 0);
    lv_obj_clear_flag(side, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(side, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(side, 0, 0);
    lv_obj_set_style_pad_all(side, 0, 0);

    s_weekday_label = make_label(side, "RTC", &lv_font_montserrat_20, kInk);
    lv_obj_set_pos(s_weekday_label, 36, 24);
    lv_obj_set_size(s_weekday_label, 104, 24);

    s_date_label = make_label(side, "--/--", &lv_font_montserrat_20, kInk);
    lv_obj_set_pos(s_date_label, 36, 50);
    lv_obj_set_size(s_date_label, 104, 24);

    s_wifi_icon = make_label(side, LV_SYMBOL_WIFI, &lv_font_montserrat_16, kFaint);
    lv_obj_set_pos(s_wifi_icon, 36, 86);
    lv_obj_set_size(s_wifi_icon, 24, 20);

    s_sync_icon = make_label(side, LV_SYMBOL_REFRESH, &lv_font_montserrat_16, kMuted);
    lv_obj_set_pos(s_sync_icon, 72, 86);
    lv_obj_set_size(s_sync_icon, 24, 20);

    s_power_icon = make_label(side, LV_SYMBOL_CHARGE, &lv_font_montserrat_16, kFaint);
    lv_obj_set_pos(s_power_icon, 108, 86);
    lv_obj_set_size(s_power_icon, 24, 20);

    s_battery_shell = lv_obj_create(side);
    lv_obj_set_size(s_battery_shell, 46, 22);
    lv_obj_set_pos(s_battery_shell, 36, 124);
    lv_obj_clear_flag(s_battery_shell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_battery_shell, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_battery_shell, 2, 0);
    lv_obj_set_style_border_color(s_battery_shell, lv_color_hex(kMuted), 0);
    lv_obj_set_style_radius(s_battery_shell, 4, 0);
    lv_obj_set_style_pad_all(s_battery_shell, 0, 0);

    s_battery_fill = lv_obj_create(s_battery_shell);
    lv_obj_set_size(s_battery_fill, 0, 14);
    lv_obj_set_pos(s_battery_fill, 2, 2);
    lv_obj_clear_flag(s_battery_fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_battery_fill, lv_color_hex(kInk), 0);
    lv_obj_set_style_bg_opa(s_battery_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_battery_fill, 0, 0);
    lv_obj_set_style_radius(s_battery_fill, 2, 0);
    lv_obj_set_style_pad_all(s_battery_fill, 0, 0);

    s_battery_cap = lv_obj_create(side);
    lv_obj_set_size(s_battery_cap, 5, 12);
    lv_obj_set_pos(s_battery_cap, 84, 129);
    lv_obj_clear_flag(s_battery_cap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_battery_cap, lv_color_hex(kMuted), 0);
    lv_obj_set_style_bg_opa(s_battery_cap, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_battery_cap, 0, 0);
    lv_obj_set_style_radius(s_battery_cap, 2, 0);
    lv_obj_set_style_pad_all(s_battery_cap, 0, 0);

    s_battery_label = make_label(side, "--%", &lv_font_montserrat_16, kInk);
    lv_obj_set_pos(s_battery_label, 100, 124);
    lv_obj_set_size(s_battery_label, 52, 22);
    lv_obj_set_style_text_align(s_battery_label, LV_TEXT_ALIGN_LEFT, 0);

    lv_timer_create(update_cb, 1000, nullptr);
    update_battery_ui(true);
    update_cb(nullptr);
}

extern "C" void clock_ui_set_dimmed(bool dimmed)
{
    s_dimmed = dimmed;
    render_time(s_last_time);
    if (s_battery_display_percent >= 0) {
        set_battery_gauge(s_battery_display_percent);
    }
}

extern "C" void clock_ui_set_external_power(bool external_power)
{
    s_external_power = external_power;
    if (s_power_icon != nullptr) {
        update_network_icons();
    }
}
