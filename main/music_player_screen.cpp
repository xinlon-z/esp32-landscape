#include "music_player_screen.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "extra/libs/sjpg/tjpgd.h"
#include "extra/libs/tiny_ttf/lv_tiny_ttf.h"
#include "lvgl.h"
#include "music_background.h"
#include "music_mqtt.h"
#include "music_visualizer.h"

#ifdef SIM_BUILD
#include "sim_assets.h"
#else
extern const uint8_t noto_sans_sc_subset_ttf_start[] asm("_binary_NotoSansSCSubset_ttf_start");
extern const uint8_t noto_sans_sc_subset_ttf_end[] asm("_binary_NotoSansSCSubset_ttf_end");
#endif

namespace {
constexpr const char* kTag = "music_player";
constexpr uint32_t kBg0      = 0x050507;
constexpr uint32_t kBg1      = 0x080a10;
constexpr uint32_t kSurface  = 0x1d1d26;
constexpr uint32_t kText     = 0xf8f8fa;
constexpr uint32_t kMuted    = 0xb4b4ba;
constexpr uint32_t kAccent   = 0xfa2d55;
constexpr uint32_t kCoverA   = 0xd85c52;
constexpr uint32_t kCoverB   = 0x24364b;
constexpr uint32_t kCoverC   = 0xe7d4bb;

constexpr int kScreenW      = 640;
constexpr int kScreenH      = 172;
constexpr int kCoverSize    = 144;
constexpr int kCoverDisplaySize = 132;
constexpr int kStageX       = 54;
constexpr int kStageY       = 20;
constexpr int kStageW       = 540;
constexpr int kStageH       = 132;
constexpr int kTextX        = 220;
constexpr int kSpectrumX    = 220;
constexpr int kSpectrumY    = 103;
constexpr int kSpectrumBarW = 3;
constexpr int kSpectrumGap  = 3;
constexpr int kButtonMain   = 46;
constexpr uint32_t kFramesPerSecond = 44100;
constexpr size_t kJpegWorkBytes = 4096;
constexpr size_t kMusicFontCacheBytes = 64 * 1024;

struct JpegDecodeContext {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    uint32_t pos = 0;
    lv_color_t* pixels = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
};

lv_font_t* s_music_font = nullptr;
lv_font_t* s_music_font_small = nullptr;

lv_color_t* allocCoverPixels(size_t count)
{
    lv_color_t* pixels = static_cast<lv_color_t*>(
        heap_caps_malloc(count * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!pixels) {
        pixels = static_cast<lv_color_t*>(
            heap_caps_malloc(count * sizeof(lv_color_t), MALLOC_CAP_8BIT));
    }
    return pixels;
}

void clearStyle(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

void clearRectStyle(lv_obj_t* obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_clip_corner(obj, false, 0);
}

void setBg(lv_obj_t* obj, uint32_t color, lv_opa_t opa)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
}

void formatTime(uint32_t seconds, char* out, size_t out_size)
{
    snprintf(out, out_size, "%lu:%02lu",
             static_cast<unsigned long>(seconds / 60u),
             static_cast<unsigned long>(seconds % 60u));
}

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

const lv_font_t* musicTextFont()
{
    if (!s_music_font) {
        s_music_font = createMusicFont(21);
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
        s_music_font_small = createMusicFont(17);
    }
    return s_music_font_small ? s_music_font_small : &lv_font_simsun_16_cjk;
}

size_t jpegInput(JDEC* jd, uint8_t* buff, size_t ndata)
{
    auto* ctx = static_cast<JpegDecodeContext*>(jd->device);
    if (!ctx || ctx->pos >= ctx->size) {
        return 0;
    }

    const uint32_t left = ctx->size - ctx->pos;
    const uint32_t read = ndata < left ? static_cast<uint32_t>(ndata) : left;
    if (buff) {
        memcpy(buff, ctx->data + ctx->pos, read);
    }
    ctx->pos += read;
    return read;
}

int jpegOutput(JDEC* jd, void* bitmap, JRECT* rect)
{
    auto* ctx = static_cast<JpegDecodeContext*>(jd->device);
    if (!ctx || !ctx->pixels || !bitmap || !rect) {
        return 0;
    }

    const uint8_t* src = static_cast<const uint8_t*>(bitmap);
    for (uint16_t y = rect->top; y <= rect->bottom; ++y) {
        for (uint16_t x = rect->left; x <= rect->right; ++x) {
            const uint8_t r = *src++;
            const uint8_t g = *src++;
            const uint8_t b = *src++;
            if (x < ctx->width && y < ctx->height) {
                ctx->pixels[y * ctx->width + x] = lv_color_make(r, g, b);
            }
        }
    }
    return 1;
}

lv_color_t* resampleCoverToSquare(const lv_color_t* src, uint16_t src_w, uint16_t src_h)
{
    if (!src || src_w == 0 || src_h == 0) {
        return nullptr;
    }

    lv_color_t* dst = allocCoverPixels(kCoverSize * kCoverSize);
    if (!dst) {
        return nullptr;
    }

    uint16_t crop_x = 0;
    uint16_t crop_y = 0;
    uint16_t crop_w = src_w;
    uint16_t crop_h = src_h;
    if (src_w > src_h) {
        crop_w = src_h;
        crop_x = static_cast<uint16_t>((src_w - crop_w) / 2u);
    } else if (src_h > src_w) {
        crop_h = src_w;
        crop_y = static_cast<uint16_t>((src_h - crop_h) / 2u);
    }

    for (uint16_t y = 0; y < kCoverSize; ++y) {
        uint16_t sy = static_cast<uint16_t>(crop_y + (static_cast<uint32_t>(y) * crop_h) / kCoverSize);
        if (sy >= src_h) {
            sy = static_cast<uint16_t>(src_h - 1);
        }
        for (uint16_t x = 0; x < kCoverSize; ++x) {
            uint16_t sx = static_cast<uint16_t>(crop_x + (static_cast<uint32_t>(x) * crop_w) / kCoverSize);
            if (sx >= src_w) {
                sx = static_cast<uint16_t>(src_w - 1);
            }
            dst[y * kCoverSize + x] = src[sy * src_w + sx];
        }
    }

    return dst;
}

uint32_t progressTotalFrames(const MusicState& state)
{
    return state.progress_end_frame - state.progress_start_frame;
}

uint32_t progressElapsedFramesForUi(const MusicState& state)
{
    const uint32_t total = progressTotalFrames(state);
    if (total == 0) {
        return 0;
    }

    uint32_t elapsed = state.progress_current_frame - state.progress_start_frame;
    if (state.playing && state.last_progress_ms != 0) {
        elapsed += (lv_tick_elaps(state.last_progress_ms) * kFramesPerSecond) / 1000u;
    }
    return elapsed > total ? total : elapsed;
}

} // namespace

lv_obj_t* MusicPlayerScreen::makeLabel(lv_obj_t* parent, const char* text,
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

lv_obj_t* MusicPlayerScreen::makePanel(lv_obj_t* parent, int x, int y, int w, int h,
                                       uint32_t color, lv_opa_t opa)
{
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_pos(panel, x, y);
    clearStyle(panel);
    setBg(panel, color, opa);
    lv_obj_set_style_radius(panel, 18, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_opa(panel, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(panel, 18, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_y(panel, 8, 0);
    return panel;
}

lv_obj_t* MusicPlayerScreen::makeRoundButton(lv_obj_t* parent, int x, int y, int size, bool primary)
{
    lv_obj_t* button = lv_obj_create(parent);
    lv_obj_set_size(button, size, size);
    lv_obj_set_pos(button, x, y);
    clearStyle(button);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    setBg(button, primary ? 0xf8f8fa : 0xffffff, primary ? LV_OPA_COVER : LV_OPA_20);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_opa(button, primary ? LV_OPA_60 : LV_OPA_40, 0);
    lv_obj_set_style_shadow_width(button, primary ? 8 : 3, 0);
    lv_obj_set_style_shadow_color(button, lv_color_hex(primary ? 0xffffff : 0x000000), 0);
    lv_obj_set_style_shadow_opa(button, primary ? LV_OPA_20 : LV_OPA_10, 0);
    return button;
}

void MusicPlayerScreen::makePrevIcon(lv_obj_t* parent, uint32_t color)
{
    lv_obj_t* label = makeLabel(parent, LV_SYMBOL_PREV, &lv_font_montserrat_16, color);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

void MusicPlayerScreen::makePauseIcon(lv_obj_t* parent, uint32_t color)
{
    lv_obj_t* label = makeLabel(parent, LV_SYMBOL_PAUSE, &lv_font_montserrat_20, color);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

void MusicPlayerScreen::makeNextIcon(lv_obj_t* parent, uint32_t color)
{
    lv_obj_t* label = makeLabel(parent, LV_SYMBOL_NEXT, &lv_font_montserrat_16, color);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

void MusicPlayerScreen::create()
{
    lv_obj_t* screen = lv_scr_act();
    lv_obj_clean(screen);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_clip_corner(screen, false, 0);
    setBg(screen, kBg0, LV_OPA_COVER);

    lv_obj_t* bg = lv_obj_create(screen);
    clearRectStyle(bg);
    lv_obj_set_size(bg, kScreenW, kScreenH);
    lv_obj_set_pos(bg, 0, 0);
    setBg(bg, kBg1, LV_OPA_COVER);
    lv_obj_set_style_bg_grad_color(bg, lv_color_hex(0x101827), 0);
    lv_obj_set_style_bg_grad_dir(bg, LV_GRAD_DIR_HOR, 0);

    background_img_ = lv_img_create(bg);
    lv_obj_set_size(background_img_, kScreenW, kScreenH);
    lv_obj_set_pos(background_img_, 0, 0);
    lv_obj_set_style_radius(background_img_, 0, 0);
    lv_obj_set_style_clip_corner(background_img_, false, 0);
    lv_obj_add_flag(background_img_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* scrim = lv_obj_create(bg);
    clearRectStyle(scrim);
    lv_obj_set_size(scrim, kScreenW, kScreenH);
    lv_obj_set_pos(scrim, 0, 0);
    setBg(scrim, 0x020306, LV_OPA_50);

    lv_obj_t* stage = lv_obj_create(bg);
    clearRectStyle(stage);
    lv_obj_set_size(stage, kStageW, kStageH);
    lv_obj_set_pos(stage, kStageX, kStageY);

    lv_obj_t* cover = lv_obj_create(stage);
    lv_obj_set_size(cover, kCoverDisplaySize, kCoverDisplaySize);
    lv_obj_set_pos(cover, 34, 0);
    clearStyle(cover);
    lv_obj_set_style_radius(cover, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(cover, 1, 0);
    lv_obj_set_style_border_color(cover, lv_color_hex(0xd9dde3), 0);
    lv_obj_set_style_border_opa(cover, LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(cover, 10, 0);
    lv_obj_set_style_shadow_color(cover, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(cover, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_y(cover, 4, 0);
    lv_obj_set_style_clip_corner(cover, true, 0);
    setBg(cover, kCoverB, LV_OPA_COVER);

    cover_band_ = lv_obj_create(cover);
    lv_obj_set_size(cover_band_, kCoverDisplaySize, 42);
    lv_obj_set_pos(cover_band_, 0, 90);
    clearStyle(cover_band_);
    setBg(cover_band_, kCoverC, LV_OPA_80);

    cover_accent_ = lv_obj_create(cover);
    lv_obj_set_size(cover_accent_, 48, 48);
    lv_obj_set_pos(cover_accent_, 42, 31);
    clearStyle(cover_accent_);
    lv_obj_set_style_radius(cover_accent_, LV_RADIUS_CIRCLE, 0);
    setBg(cover_accent_, kCoverA, LV_OPA_70);

    cover_img_ = lv_img_create(cover);
    lv_obj_set_size(cover_img_, kCoverSize, kCoverSize);
    lv_obj_center(cover_img_);
    lv_obj_add_flag(cover_img_, LV_OBJ_FLAG_HIDDEN);

    const lv_font_t* text_font = musicTextFont();
    const lv_font_t* small_text_font = musicSmallTextFont();

    title_ = makeLabel(stage, state_.title, text_font, kText);
    lv_obj_set_pos(title_, kTextX, 16);
    lv_obj_set_size(title_, 184, 31);
    lv_label_set_long_mode(title_, LV_LABEL_LONG_DOT);

    subtitle_ = makeLabel(stage, "", small_text_font, 0x9aa5ad);
    lv_obj_set_pos(subtitle_, kTextX, 51);
    lv_obj_set_size(subtitle_, 184, 22);
    lv_label_set_long_mode(subtitle_, LV_LABEL_LONG_DOT);

    progress_ = lv_bar_create(stage);
    lv_obj_set_size(progress_, 1, 1);
    lv_obj_set_pos(progress_, 0, 0);
    clearStyle(progress_);
    lv_bar_set_range(progress_, 0, 1000);
    lv_obj_add_flag(progress_, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < kSpectrumBarCount; ++i) {
        lv_obj_t* bar = lv_obj_create(stage);
        lv_obj_set_size(bar, kSpectrumBarW, 8);
        lv_obj_set_pos(bar, kSpectrumX + i * (kSpectrumBarW + kSpectrumGap), kSpectrumY - 8);
        clearStyle(bar);
        lv_obj_set_style_radius(bar, 1, 0);
        setBg(bar, 0xffffff, i < 18 ? LV_OPA_COVER : LV_OPA_30);
        spectrum_bars_[i] = bar;
    }

    elapsed_ = makeLabel(stage, "0:00", &lv_font_montserrat_12, 0x8f9aa3);
    lv_obj_set_pos(elapsed_, kTextX, 114);
    lv_obj_set_size(elapsed_, 46, 18);

    duration_ = makeLabel(stage, "0:00", &lv_font_montserrat_12, 0x8f9aa3);
    lv_obj_set_pos(duration_, kTextX + 48, 114);
    lv_obj_set_size(duration_, 80, 18);

    lv_obj_t* volume_icon = makeLabel(stage, LV_SYMBOL_VOLUME_MID, &lv_font_montserrat_12, 0x8f9aa3);
    lv_obj_set_pos(volume_icon, 426, 113);
    lv_obj_set_size(volume_icon, 24, 20);

    lv_obj_t* audio_icon = makeLabel(stage, LV_SYMBOL_AUDIO, &lv_font_montserrat_12, 0x8f9aa3);
    lv_obj_set_pos(audio_icon, 456, 113);
    lv_obj_set_size(audio_icon, 24, 20);

    lv_obj_t* video_icon = makeLabel(stage, LV_SYMBOL_VIDEO, &lv_font_montserrat_12, 0x8f9aa3);
    lv_obj_set_pos(video_icon, 486, 113);
    lv_obj_set_size(video_icon, 24, 20);

    lv_obj_t* pause = makeRoundButton(stage, 450, 20, kButtonMain, true);
    play_pause_icon_ = makeLabel(pause, LV_SYMBOL_PAUSE, &lv_font_montserrat_20, 0x050507);
    lv_obj_set_style_text_align(play_pause_icon_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(play_pause_icon_);

    timer_ = lv_timer_create(onTimer, 1000, this);
    updateUi();
}

void MusicPlayerScreen::destroy()
{
    if (timer_) {
        lv_timer_del(timer_);
        timer_ = nullptr;
    }
    if (cover_pixels_) {
        heap_caps_free(cover_pixels_);
        cover_pixels_ = nullptr;
    }
    if (background_pixels_) {
        heap_caps_free(background_pixels_);
        background_pixels_ = nullptr;
    }
    if (stale_cover_pixels_) {
        heap_caps_free(stale_cover_pixels_);
        stale_cover_pixels_ = nullptr;
    }
    if (stale_background_pixels_) {
        heap_caps_free(stale_background_pixels_);
        stale_background_pixels_ = nullptr;
    }
    title_ = subtitle_ = progress_ = elapsed_ = duration_ = play_pause_icon_ = background_img_ = cover_img_ = nullptr;
    for (lv_obj_t*& bar : spectrum_bars_) {
        bar = nullptr;
    }
    cover_band_ = cover_accent_ = nullptr;
    cover_dsc_ = {};
    background_dsc_ = {};
}

void MusicPlayerScreen::onTimer(lv_timer_t* timer)
{
    static_cast<MusicPlayerScreen*>(timer->user_data)->updateUi();
}

void MusicPlayerScreen::updateUi()
{
    if (!title_ || !subtitle_ || !progress_ || !elapsed_ || !duration_) {
        return;
    }

    MusicMqtt::getState(&state_);
    updateCover();

    char subtitle[200];
    if (state_.artist[0] && state_.album[0]) {
        snprintf(subtitle, sizeof(subtitle), "%s - %s", state_.artist, state_.album);
    } else if (state_.artist[0]) {
        snprintf(subtitle, sizeof(subtitle), "%s", state_.artist);
    } else {
        snprintf(subtitle, sizeof(subtitle), "%s", state_.album);
    }
    lv_label_set_text(title_, state_.title);
    lv_label_set_text(subtitle_, subtitle);

    const uint32_t total_frames = progressTotalFrames(state_);
    const uint32_t elapsed_frames = progressElapsedFramesForUi(state_);
    const uint32_t progress = total_frames == 0 ? 0 : (elapsed_frames * 1000u) / total_frames;
    const uint32_t clamped_progress = progress > 1000u ? 1000u : progress;
    lv_bar_set_value(progress_, static_cast<int32_t>(clamped_progress), LV_ANIM_OFF);
    for (int i = 0; i < kSpectrumBarCount; ++i) {
        lv_obj_t* bar = spectrum_bars_[i];
        if (!bar) {
            continue;
        }
        const uint8_t h = musicVisualizerBarHeight(static_cast<uint8_t>(i),
                                                   static_cast<uint8_t>(kSpectrumBarCount),
                                                   clamped_progress,
                                                   state_.playing);
        lv_obj_set_size(bar, kSpectrumBarW, h);
        lv_obj_set_y(bar, kSpectrumY - h);
        setBg(bar, 0xffffff, i < static_cast<int>((clamped_progress * kSpectrumBarCount) / 1000u)
                                  ? LV_OPA_COVER
                                  : LV_OPA_30);
    }
    if (play_pause_icon_) {
        lv_label_set_text(play_pause_icon_, state_.playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
        lv_obj_center(play_pause_icon_);
    }

    char elapsed[12];
    char duration[12];
    formatTime(elapsed_frames / kFramesPerSecond, elapsed, sizeof(elapsed));
    formatTime(musicDurationSeconds(state_), duration, sizeof(duration));
    lv_label_set_text(elapsed_, elapsed);
    lv_label_set_text(duration_, duration);
}

void MusicPlayerScreen::updateCover()
{
    if (!cover_img_) {
        return;
    }

    MusicMqtt::CoverImage cover;
    if (!MusicMqtt::takeCover(&cover)) {
        if (cover_pixels_ || !MusicMqtt::copyLastCover(&cover)) {
            return;
        }
    }
    if (cover.size < 128) {
        heap_caps_free(cover.data);
        return;
    }

    if (!decodeCoverJpeg(cover.data, cover.size)) {
        heap_caps_free(cover.data);
        return;
    }
    heap_caps_free(cover.data);

    updateBackgroundFromCover();

    lv_img_cache_invalidate_src(&cover_dsc_);
    lv_img_set_src(cover_img_, &cover_dsc_);
    lv_img_set_zoom(cover_img_, (kCoverDisplaySize * LV_IMG_ZOOM_NONE) / kCoverSize);
    lv_obj_center(cover_img_);
    lv_obj_clear_flag(cover_img_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(cover_img_);
    lv_obj_invalidate(cover_img_);
    if (cover_band_) {
        lv_obj_add_flag(cover_band_, LV_OBJ_FLAG_HIDDEN);
    }
    if (cover_accent_) {
        lv_obj_add_flag(cover_accent_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool MusicPlayerScreen::updateBackgroundFromCover()
{
    if (!background_img_ || !cover_pixels_) {
        return false;
    }

    lv_color_t* pixels = allocCoverPixels(kScreenW * kScreenH);
    if (!pixels) {
        ESP_LOGW(kTag, "blurred background allocation failed");
        return false;
    }
    lv_color_t* scratch = allocCoverPixels(kScreenW * kScreenH);
    if (!scratch) {
        heap_caps_free(pixels);
        ESP_LOGW(kTag, "blurred background scratch allocation failed");
        return false;
    }

    const bool ok = musicGenerateBlurredBackground(cover_pixels_, kCoverSize, kCoverSize,
                                                  pixels, kScreenW, kScreenH, scratch);
    heap_caps_free(scratch);
    if (!ok) {
        heap_caps_free(pixels);
        return false;
    }

    lv_img_cache_invalidate_src(&background_dsc_);
    if (stale_background_pixels_) {
        heap_caps_free(stale_background_pixels_);
    }
    stale_background_pixels_ = background_pixels_;
    background_pixels_ = pixels;
    background_dsc_ = {};
    background_dsc_.header.always_zero = 0;
    background_dsc_.header.w = kScreenW;
    background_dsc_.header.h = kScreenH;
    background_dsc_.header.cf = LV_IMG_CF_TRUE_COLOR;
    background_dsc_.data = reinterpret_cast<const uint8_t*>(background_pixels_);
    background_dsc_.data_size = kScreenW * kScreenH * sizeof(lv_color_t);

    lv_img_set_src(background_img_, &background_dsc_);
    lv_obj_clear_flag(background_img_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(background_img_);
    lv_obj_invalidate(background_img_);
    return true;
}

bool MusicPlayerScreen::decodeCoverJpeg(uint8_t* data, uint32_t size)
{
    if (!data || size < 4 || data[0] != 0xff || data[1] != 0xd8) {
        ESP_LOGW(kTag, "cover is not a JPEG payload");
        return false;
    }

    uint8_t* work = static_cast<uint8_t*>(heap_caps_malloc(kJpegWorkBytes, MALLOC_CAP_8BIT));
    if (!work) {
        return false;
    }

    JpegDecodeContext probe = {};
    probe.data = data;
    probe.size = size;

    JDEC jd = {};
    JRESULT rc = jd_prepare(&jd, jpegInput, work, kJpegWorkBytes, &probe);
    if (rc != JDR_OK) {
        ESP_LOGW(kTag, "JPEG prepare failed: %d", static_cast<int>(rc));
        heap_caps_free(work);
        return false;
    }

    uint8_t scale = 0;
    while (scale < 3 && ((jd.width >> scale) > kCoverSize || (jd.height >> scale) > kCoverSize)) {
        ++scale;
    }
    uint16_t out_w = static_cast<uint16_t>(jd.width >> scale);
    uint16_t out_h = static_cast<uint16_t>(jd.height >> scale);
    if (out_w == 0) {
        out_w = 1;
    }
    if (out_h == 0) {
        out_h = 1;
    }

    lv_color_t* decoded_pixels = allocCoverPixels(out_w * out_h);
    if (!decoded_pixels) {
        heap_caps_free(work);
        return false;
    }

    JpegDecodeContext decode = {};
    decode.data = data;
    decode.size = size;
    decode.pixels = decoded_pixels;
    decode.width = out_w;
    decode.height = out_h;
    memset(decoded_pixels, 0, out_w * out_h * sizeof(lv_color_t));

    rc = jd_prepare(&jd, jpegInput, work, kJpegWorkBytes, &decode);
    if (rc == JDR_OK) {
        rc = jd_decomp(&jd, jpegOutput, scale);
    }
    heap_caps_free(work);

    if (rc != JDR_OK) {
        ESP_LOGW(kTag, "JPEG decode failed: %d", static_cast<int>(rc));
        heap_caps_free(decoded_pixels);
        return false;
    }

    lv_color_t* pixels = resampleCoverToSquare(decoded_pixels, out_w, out_h);
    heap_caps_free(decoded_pixels);
    if (!pixels) {
        return false;
    }

    if (stale_cover_pixels_) {
        heap_caps_free(stale_cover_pixels_);
    }
    stale_cover_pixels_ = cover_pixels_;
    cover_pixels_ = pixels;
    cover_dsc_ = {};
    cover_dsc_.header.always_zero = 0;
    cover_dsc_.header.w = kCoverSize;
    cover_dsc_.header.h = kCoverSize;
    cover_dsc_.header.cf = LV_IMG_CF_TRUE_COLOR;
    cover_dsc_.data = reinterpret_cast<const uint8_t*>(cover_pixels_);
    cover_dsc_.data_size = kCoverSize * kCoverSize * sizeof(lv_color_t);
    ESP_LOGI(kTag, "cover decoded: %ux%u -> %ux%u -> %dx%d",
             jd.width, jd.height, out_w, out_h, kCoverSize, kCoverSize);
    return true;
}
