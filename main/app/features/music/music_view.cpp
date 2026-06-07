#include "music_view.h"

#include "app/ui/fonts/music_fonts.h"
#include "util/music_player_icon_geometry.h"

#include <string.h>

namespace {
constexpr uint32_t kBg0 = 0x050507;
constexpr uint32_t kBg1 = 0x080a10;
constexpr uint32_t kText = 0xf8f8fa;

constexpr int kScreenW = 640;
constexpr int kScreenH = 172;
constexpr int kStageX = 44;
constexpr int kStageY = 15;
constexpr int kStageW = 552;
constexpr int kStageH = 142;
constexpr int kTextX = 214;
constexpr int kButtonMain = 52;
constexpr int kBottomIconY = 123;
constexpr int kBottomIconBoxW = 24;
constexpr int kBottomIconBoxH = 20;
constexpr int kBottomIconStartX = 420;
constexpr int kBottomIconStep = 34;
constexpr const char* kSymbolVolumeUp = "\xEE\x81\x90"; /* Material Icons U+E050 */
constexpr const char* kSymbolAirPlay = "\xEE\x81\x95"; /* Material Icons U+E055 */
constexpr const char* kSymbolMusicNote = "\xEE\x90\x85"; /* Material Icons U+E405 */

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

lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    return label;
}

lv_obj_t* makeRoundButton(lv_obj_t* parent, int x, int y, int size, bool primary)
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

void alignPlayPauseIcon(lv_obj_t* icon, bool playing)
{
    const MusicIconOffset offset = musicPlayPauseIconOffset(playing);
    lv_obj_align(icon, LV_ALIGN_CENTER, offset.x, offset.y);
}

lv_obj_t* makeBottomIcon(lv_obj_t* parent, int index, const char* symbol,
                         const lv_font_t* font, int y_offset = 0)
{
    lv_obj_t* box = lv_obj_create(parent);
    clearStyle(box);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_pos(box, kBottomIconStartX + index * kBottomIconStep, kBottomIconY);
    lv_obj_set_size(box, kBottomIconBoxW, kBottomIconBoxH);

    lv_obj_t* icon = lv_label_create(box);
    clearStyle(icon);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, font, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0x8f9aa3), 0);
    lv_obj_set_style_text_opa(icon, LV_OPA_COVER, 0);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, y_offset);
    return box;
}
} // namespace

extern "C" const lv_font_t material_status_icons_20;

void MusicView::create()
{
    has_last_state_ = false;
    last_state_ = MusicDisplayState{};

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

    background_.create(bg);

    lv_obj_t* scrim = lv_obj_create(bg);
    clearRectStyle(scrim);
    lv_obj_set_size(scrim, kScreenW, kScreenH);
    lv_obj_set_pos(scrim, 0, 0);
    setBg(scrim, 0x020306, LV_OPA_50);

    lv_obj_t* stage = lv_obj_create(bg);
    clearRectStyle(stage);
    lv_obj_set_size(stage, kStageW, kStageH);
    lv_obj_set_pos(stage, kStageX, kStageY);

    cover_.create(stage);

    const lv_font_t* text_font = musicTextFont();
    const lv_font_t* small_text_font = musicSmallTextFont();

    title_ = makeLabel(stage, "", text_font, kText);
    lv_obj_set_pos(title_, kTextX, 14);
    lv_obj_set_size(title_, 226, 34);
    lv_label_set_long_mode(title_, LV_LABEL_LONG_DOT);

    subtitle_ = makeLabel(stage, "", small_text_font, 0x9aa5ad);
    lv_obj_set_pos(subtitle_, kTextX, 50);
    lv_obj_set_size(subtitle_, 226, 24);
    lv_label_set_long_mode(subtitle_, LV_LABEL_LONG_DOT);

    visualizer_.create(stage);

    time_ = makeLabel(stage, "0:00/0:00", &lv_font_montserrat_14, 0x8f9aa3);
    lv_obj_set_pos(time_, kTextX, 124);
    lv_obj_set_size(time_, 116, 18);

    makeBottomIcon(stage, 0, kSymbolVolumeUp, &material_status_icons_20, 0);
    makeBottomIcon(stage, 1, kSymbolMusicNote, &material_status_icons_20, 0);
    makeBottomIcon(stage, 2, kSymbolAirPlay, &material_status_icons_20, 0);

    lv_obj_t* pause = makeRoundButton(stage, 462, 18, kButtonMain, true);
    play_pause_icon_ = makeLabel(pause, LV_SYMBOL_PAUSE, &lv_font_montserrat_20, 0x050507);
    lv_obj_set_style_text_align(play_pause_icon_, LV_TEXT_ALIGN_CENTER, 0);
    alignPlayPauseIcon(play_pause_icon_, true);
}

void MusicView::destroy()
{
    title_ = nullptr;
    subtitle_ = nullptr;
    time_ = nullptr;
    play_pause_icon_ = nullptr;
    has_last_state_ = false;
    last_state_ = MusicDisplayState{};
    background_.clear();
    cover_.clear();
    visualizer_.clear();
}

void MusicView::render(const MusicDisplayState& state)
{
    if (!title_ || !subtitle_ || !time_) {
        return;
    }

    if (!has_last_state_ || strcmp(last_state_.title, state.title) != 0) {
        lv_label_set_text(title_, state.title);
    }
    if (!has_last_state_ || strcmp(last_state_.subtitle, state.subtitle) != 0) {
        lv_label_set_text(subtitle_, state.subtitle);
    }
    if (!has_last_state_ || strcmp(last_state_.time, state.time) != 0) {
        lv_label_set_text(time_, state.time);
    }
    if (!has_last_state_ ||
        last_state_.progress_per_mille != state.progress_per_mille ||
        last_state_.playing != state.playing) {
        visualizer_.render(state.progress_per_mille, state.playing);
    }
    if (play_pause_icon_) {
        if (!has_last_state_ || last_state_.playing != state.playing) {
            lv_label_set_text(play_pause_icon_, state.playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
            alignPlayPauseIcon(play_pause_icon_, state.playing);
        }
    }
    last_state_ = state;
    has_last_state_ = true;
}

void MusicView::renderCover(const BorrowedCover& cover)
{
    background_.renderCover(cover);
    cover_.renderCover(cover);
}

void MusicView::renderCoverPlaceholder()
{
    background_.renderPlaceholder();
    cover_.renderPlaceholder();
}

void MusicView::setDimmed(bool dimmed)
{
    background_.setBlurEnabled(!dimmed);
    visualizer_.setDimmed(dimmed);
}
