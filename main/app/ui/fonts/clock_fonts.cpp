#include "clock_fonts.h"

#include "clock_glyph_mask.h"
#include "esp_log.h"
#include "extra/libs/tiny_ttf/lv_tiny_ttf.h"

#include <stddef.h>
#include <stdint.h>

#ifdef SIM_BUILD
#include "sim_assets.h"
#else
extern const uint8_t noto_sans_sc_subset_ttf_start[] asm("_binary_NotoSansSCSubset_ttf_start");
extern const uint8_t noto_sans_sc_subset_ttf_end[] asm("_binary_NotoSansSCSubset_ttf_end");
#endif

namespace {
constexpr const char* kTag = "clock_fonts";
constexpr uint16_t kClockTimeFontSize = 154;
constexpr size_t kClockFontCacheBytes = 64 * 1024;

struct ClockTabularFontDsc {
    const lv_font_t* base = nullptr;
    uint16_t digit_adv = 0;
    uint16_t colon_adv = 0;
    int16_t colon_ofs_y_adjust = 0;
};

lv_font_t* s_clock_time_base_font = nullptr;
lv_font_t s_clock_time_font{};
ClockTabularFontDsc s_clock_time_dsc{};
const lv_font_t* s_clock_time_ready_font = nullptr;
ClockFontVisualMetrics s_clock_time_metrics{};

uint16_t glyphAdvance(const lv_font_t* font, uint32_t letter)
{
    lv_font_glyph_dsc_t glyph{};
    return font && lv_font_get_glyph_dsc(font, &glyph, letter, 0) ? glyph.adv_w : 0;
}

bool glyphBounds(const lv_font_t* font, uint32_t letter, int& top, int& bottom)
{
    lv_font_glyph_dsc_t glyph{};
    if (!font || !lv_font_get_glyph_dsc(font, &glyph, letter, 0) || glyph.box_h == 0) {
        return false;
    }

    top = font->line_height - font->base_line - glyph.box_h - glyph.ofs_y;
    bottom = top + glyph.box_h;
    return true;
}

ClockFontVisualMetrics measureDigitMetrics(const lv_font_t* font)
{
    ClockFontVisualMetrics metrics{};
    if (!font) {
        return metrics;
    }

    metrics.line_height = font->line_height;
    bool initialized = false;
    for (uint32_t letter = '0'; letter <= '9'; ++letter) {
        int top = 0;
        int bottom = 0;
        if (!glyphBounds(font, letter, top, bottom)) {
            continue;
        }

        if (!initialized) {
            metrics.digit_top = top;
            metrics.digit_bottom = bottom;
            initialized = true;
        } else {
            if (top < metrics.digit_top) {
                metrics.digit_top = top;
            }
            if (bottom > metrics.digit_bottom) {
                metrics.digit_bottom = bottom;
            }
        }
    }

    if (!initialized) {
        metrics.digit_top = 0;
        metrics.digit_bottom = metrics.line_height;
    }
    return metrics;
}

int glyphCenter(const lv_font_t* font, uint32_t letter)
{
    int top = 0;
    int bottom = 0;
    return glyphBounds(font, letter, top, bottom) ? (top + bottom) / 2 : 0;
}

uint16_t maxDigitAdvance(const lv_font_t* font)
{
    uint16_t adv = 0;
    for (uint32_t letter = '0'; letter <= '9'; ++letter) {
        const uint16_t glyph_adv = glyphAdvance(font, letter);
        if (glyph_adv > adv) {
            adv = glyph_adv;
        }
    }
    return adv;
}

bool clockTabularGlyphDsc(const lv_font_t* font, lv_font_glyph_dsc_t* out,
                          uint32_t letter, uint32_t)
{
    auto* dsc = static_cast<const ClockTabularFontDsc*>(font ? font->dsc : nullptr);
    if (!dsc || !dsc->base || !out) {
        return false;
    }

    if (letter == ' ') {
        *out = {};
        out->adv_w = dsc->colon_adv;
        out->box_w = 0;
        out->box_h = 0;
        out->ofs_x = 0;
        out->ofs_y = 0;
        out->bpp = 0;
        out->is_placeholder = false;
        return true;
    }

    if (!lv_font_get_glyph_dsc(dsc->base, out, letter, 0)) {
        return false;
    }

    if (letter >= '0' && letter <= '9' && dsc->digit_adv > out->adv_w) {
        out->ofs_x += static_cast<int16_t>((dsc->digit_adv - out->adv_w) / 2);
        out->adv_w = dsc->digit_adv;
    } else if (letter == ':' && dsc->colon_adv > out->adv_w) {
        out->ofs_x += static_cast<int16_t>((dsc->colon_adv - out->adv_w) / 2);
        out->adv_w = dsc->colon_adv;
    }
    if (letter == ':') {
        out->ofs_y += dsc->colon_ofs_y_adjust;
    }

    return true;
}

const uint8_t* clockTabularGlyphBitmap(const lv_font_t* font, uint32_t letter)
{
    auto* dsc = static_cast<const ClockTabularFontDsc*>(font ? font->dsc : nullptr);
    if (!dsc || !dsc->base || letter == ' ') {
        return nullptr;
    }
    const uint8_t* bitmap = lv_font_get_glyph_bitmap(dsc->base, letter);
    if (!bitmap || dsc->base != s_clock_time_base_font) {
        return bitmap;
    }

    lv_font_glyph_dsc_t glyph{};
    if (!lv_font_get_glyph_dsc(dsc->base, &glyph, letter, 0) || glyph.bpp != 8) {
        return bitmap;
    }

    auto* mutable_bitmap = const_cast<uint8_t*>(bitmap);
    const uint32_t pixel_count = static_cast<uint32_t>(glyph.box_w) * glyph.box_h;
    for (uint32_t i = 0; i < pixel_count; ++i) {
        mutable_bitmap[i] = clockGlyphMaskOpacity(mutable_bitmap[i]);
    }
    return bitmap;
}

lv_font_t* createClockFont(uint16_t size)
{
#ifdef SIM_BUILD
    size_t font_size = 0;
    const uint8_t* font_data = SimAssets::fontData(&font_size);
#else
    const size_t font_size = noto_sans_sc_subset_ttf_end - noto_sans_sc_subset_ttf_start;
    const uint8_t* font_data = noto_sans_sc_subset_ttf_start;
#endif

    lv_font_t* font = font_data && font_size > 0
                          ? lv_tiny_ttf_create_data_ex(font_data, font_size, size, kClockFontCacheBytes)
                          : nullptr;
    if (font) {
        font->fallback = &lv_font_montserrat_48;
    }
    return font;
}

const lv_font_t* createClockTabularFont(const lv_font_t* base)
{
    if (!base) {
        return nullptr;
    }

    const uint16_t digit_adv = maxDigitAdvance(base);
    const uint16_t colon_adv = glyphAdvance(base, ':');
    if (digit_adv == 0 || colon_adv == 0) {
        return base;
    }

    s_clock_time_dsc.base = base;
    s_clock_time_dsc.digit_adv = digit_adv;
    s_clock_time_dsc.colon_adv = colon_adv;
    s_clock_time_dsc.colon_ofs_y_adjust = static_cast<int16_t>(
        glyphCenter(base, ':') - glyphCenter(base, '0'));

    s_clock_time_font = {};
    s_clock_time_font.get_glyph_dsc = clockTabularGlyphDsc;
    s_clock_time_font.get_glyph_bitmap = clockTabularGlyphBitmap;
    s_clock_time_font.line_height = base->line_height;
    s_clock_time_font.base_line = base->base_line;
    s_clock_time_font.subpx = base->subpx;
    s_clock_time_font.underline_position = base->underline_position;
    s_clock_time_font.underline_thickness = base->underline_thickness;
    s_clock_time_font.dsc = &s_clock_time_dsc;
    s_clock_time_font.fallback = base->fallback;
    return &s_clock_time_font;
}
} // namespace

const lv_font_t* clockTimeFont()
{
    if (!s_clock_time_ready_font) {
        s_clock_time_base_font = createClockFont(kClockTimeFontSize);
        const lv_font_t* base = s_clock_time_base_font ? s_clock_time_base_font : &lv_font_montserrat_48;
        s_clock_time_ready_font = createClockTabularFont(base);
        s_clock_time_metrics = measureDigitMetrics(s_clock_time_ready_font);
        if (s_clock_time_base_font) {
            ESP_LOGI(kTag, "clock time TinyTTF font ready");
        } else {
            ESP_LOGE(kTag, "failed to create clock TinyTTF font, falling back to Montserrat 48");
        }
    }
    return s_clock_time_ready_font ? s_clock_time_ready_font : &lv_font_montserrat_48;
}

ClockFontVisualMetrics clockTimeFontMetrics()
{
    if (!s_clock_time_ready_font) {
        clockTimeFont();
    }
    return s_clock_time_metrics;
}
