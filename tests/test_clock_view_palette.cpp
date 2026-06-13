#include <gtest/gtest.h>

#include <cstring>

#include "app/features/clock/clock_view.cpp"

const lv_font_t* clockTimeFont()
{
    static lv_font_t font{222, 44};
    return &font;
}

ClockFontVisualMetrics clockTimeFontMetrics()
{
    return ClockFontVisualMetrics{
        .line_height = 222,
        .digit_top = 62,
        .digit_bottom = 181,
    };
}

namespace {

ClockDisplayState makeTimeState(const char* time = "09:05")
{
    ClockDisplayState state{};
    std::strncpy(state.time, time, sizeof(state.time));
    std::strncpy(state.weekday, "Mon", sizeof(state.weekday));
    std::strncpy(state.date, "05/24", sizeof(state.date));
    return state;
}

void expectLabelColor(const char* text, uint32_t color)
{
    lv_obj_t* label = lvglFindLabelByText(text);
    ASSERT_NE(label, nullptr) << "label not found: " << text;
    EXPECT_EQ(label->text_color, lv_color_hex(color))
        << text << " color is 0x" << std::hex << lv_color_to32(label->text_color)
        << ", want 0x" << lv_color_to32(lv_color_hex(color));
}

std::vector<lv_obj_t*> findLabelsWithFont(const lv_font_t* font)
{
    std::vector<lv_obj_t*> labels;
    for (auto* obj : lvglStubState().objects) {
        if (obj && obj->kind == lv_obj_t::Kind::Label && obj->text_font == font) {
            labels.push_back(obj);
        }
    }
    return labels;
}

lv_obj_t* findClockTimeLabel()
{
    auto labels = findLabelsWithFont(clockTimeFont());
    if (labels.size() != 1) {
        return nullptr;
    }
    return labels.front();
}

lv_obj_t* findDivider()
{
    auto& objects = lvglStubState().objects;
    auto it = std::find_if(objects.begin(), objects.end(), [](const lv_obj_t* obj) {
        return obj && obj->kind == lv_obj_t::Kind::Object &&
               obj->x == 420 && obj->y == 30 && obj->w == 1 && obj->h == 112;
    });
    return it == objects.end() ? nullptr : *it;
}

void expectTimeLabelStyle(lv_obj_t* label, const char* text, uint32_t color, lv_opa_t opa)
{
    const ClockFontVisualMetrics metrics = clockTimeFontMetrics();
    ASSERT_NE(label, nullptr) << "single clock time label not found";
    EXPECT_EQ(label->x, 0);
    EXPECT_EQ(label->y, (172 - metrics.digit_top - metrics.digit_bottom) / 2);
    EXPECT_EQ(label->w, 420);
    EXPECT_EQ(label->h, metrics.line_height);
    EXPECT_GE(metrics.digit_bottom - metrics.digit_top, 112);
    EXPECT_EQ(label->label_long_mode, LV_LABEL_LONG_CLIP);
    EXPECT_EQ(label->text, text);
    EXPECT_EQ(label->text_color, lv_color_hex(color));
    EXPECT_EQ(label->text_opa, opa);
}

} // namespace

TEST(ClockViewPalette, AppliesPaletteToDateWeekdayAndBatteryText)
{
    lvglStubState().reset();

    ClockView view;
    view.create();
    view.renderBattery(BatteryDisplayState{50, true}, false);

    ClockForegroundPalette palette{};
    palette.fg = 0xf7f8fa;
    palette.dim = 0xa8b0bc;
    palette.muted = 0x778899;
    palette.faint = 0x445566;
    palette.accent = 0x8fe8c5;

    view.setPalette(palette);
    view.renderTime(makeTimeState(), false);
    view.renderBattery(BatteryDisplayState{52, false}, false);

    expectLabelColor("Mon", palette.fg);
    expectLabelColor("05/24", palette.fg);
    expectLabelColor("50%", palette.fg);

    view.destroy();
    lvglStubState().reset();
}

TEST(ClockViewPalette, RendersMainTimeAsSingleFixedOpaqueLabel)
{
    lvglStubState().reset();

    ClockView view;
    view.create();

    ClockForegroundPalette palette{};
    palette.fg = 0xf7f8fa;
    palette.dim = 0x22282b;
    palette.muted = 0x778899;
    palette.faint = 0x445566;
    palette.accent = 0x8fe8c5;

    view.setPalette(palette);
    view.renderTime(makeTimeState(), false);
    lv_obj_t* time_label = findClockTimeLabel();
    expectTimeLabelStyle(time_label, "09:05", palette.fg, LV_OPA_COVER);

    view.renderTime(makeTimeState(), true);
    EXPECT_EQ(findClockTimeLabel(), time_label);
    expectTimeLabelStyle(time_label, "09:05", palette.dim, LV_OPA_COVER);

    view.destroy();
    lvglStubState().reset();
}

TEST(ClockViewPalette, RendersDividerOpaqueAndMatchesTimeColor)
{
    lvglStubState().reset();

    ClockView view;
    view.create();

    ClockForegroundPalette palette{};
    palette.fg = 0xf7f8fa;
    palette.dim = 0x22282b;
    palette.muted = 0x778899;
    palette.faint = 0x445566;
    palette.accent = 0x8fe8c5;

    view.setPalette(palette);
    view.renderTime(makeTimeState(), false);
    lv_obj_t* divider = findDivider();
    ASSERT_NE(divider, nullptr);
    EXPECT_EQ(divider->bg_color, lv_color_hex(palette.fg));
    EXPECT_EQ(divider->bg_opa, LV_OPA_COVER);

    view.renderTime(makeTimeState(), true);
    EXPECT_EQ(divider->bg_color, lv_color_hex(palette.dim));
    EXPECT_EQ(divider->bg_opa, LV_OPA_COVER);

    view.destroy();
    lvglStubState().reset();
}

TEST(ClockViewPalette, KeepsTimeLabelGeometryStableAcrossTicks)
{
    lvglStubState().reset();

    ClockView view;
    view.create();
    ClockForegroundPalette palette{};
    palette.fg = 0xf7f8fa;
    palette.dim = 0x22282b;
    palette.muted = 0x778899;
    palette.faint = 0x445566;
    palette.accent = 0x8fe8c5;
    view.setPalette(palette);
    view.renderTime(makeTimeState("09:05"), false);

    lv_obj_t* time_label = findClockTimeLabel();
    ASSERT_NE(time_label, nullptr);
    const int x = time_label->x;
    const int y = time_label->y;
    const int w = time_label->w;
    const int h = time_label->h;

    view.renderTime(makeTimeState("09 05"), false);
    EXPECT_EQ(findClockTimeLabel(), time_label);
    EXPECT_EQ(time_label->text, "09 05");
    EXPECT_EQ(time_label->x, x);
    EXPECT_EQ(time_label->y, y);
    EXPECT_EQ(time_label->w, w);
    EXPECT_EQ(time_label->h, h);

    view.renderTime(makeTimeState("12:34"), false);
    EXPECT_EQ(findClockTimeLabel(), time_label);
    EXPECT_EQ(time_label->text, "12:34");
    EXPECT_EQ(time_label->x, x);
    EXPECT_EQ(time_label->y, y);
    EXPECT_EQ(time_label->w, w);
    EXPECT_EQ(time_label->h, h);

    view.destroy();
    lvglStubState().reset();
}
