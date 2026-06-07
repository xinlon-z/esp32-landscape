#include "music_fonts.h"

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
constexpr const char* kTag = "music_fonts";
constexpr size_t kMusicFontCacheBytes = 64 * 1024;

lv_font_t* s_music_font = nullptr;
lv_font_t* s_music_font_small = nullptr;

lv_font_t* createMusicFont(uint16_t size)
{
#ifdef SIM_BUILD
    size_t font_size = 0;
    const uint8_t* font_data = SimAssets::fontData(&font_size);
#else
    const size_t font_size = noto_sans_sc_subset_ttf_end - noto_sans_sc_subset_ttf_start;
    const uint8_t* font_data = noto_sans_sc_subset_ttf_start;
#endif
    lv_font_t* font = font_data && font_size > 0
                          ? lv_tiny_ttf_create_data_ex(font_data, font_size, size, kMusicFontCacheBytes)
                          : nullptr;
    if (font) {
        font->fallback = &lv_font_simsun_16_cjk;
    }
    return font;
}
} // namespace

const lv_font_t* musicTextFont()
{
    if (!s_music_font) {
        s_music_font = createMusicFont(23);
        if (s_music_font) {
            ESP_LOGI(kTag, "NotoSansSCSubset title font ready");
        } else {
            ESP_LOGE(kTag, "failed to create TinyTTF font, falling back to SimSun subset");
        }
    }
    return s_music_font ? s_music_font : &lv_font_simsun_16_cjk;
}

const lv_font_t* musicSmallTextFont()
{
    if (!s_music_font_small) {
        s_music_font_small = createMusicFont(18);
    }
    return s_music_font_small ? s_music_font_small : &lv_font_simsun_16_cjk;
}
