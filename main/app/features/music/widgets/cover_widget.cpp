#include "cover_widget.h"

namespace {
constexpr uint32_t kCoverA = 0xd85c52;
constexpr uint32_t kCoverB = 0x24364b;
constexpr uint32_t kCoverC = 0xe7d4bb;
constexpr int kCoverSize = 144;
constexpr int kCoverDisplaySize = 142;

void clearStyle(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

void setBg(lv_obj_t* obj, uint32_t color, lv_opa_t opa)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
}
} // namespace

void CoverWidget::create(lv_obj_t* parent)
{
    lv_obj_t* cover = lv_obj_create(parent);
    lv_obj_set_size(cover, kCoverDisplaySize, kCoverDisplaySize);
    lv_obj_set_pos(cover, 24, 0);
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
}

void CoverWidget::renderPlaceholder()
{
    if (cover_img_) {
        lv_obj_add_flag(cover_img_, LV_OBJ_FLAG_HIDDEN);
    }
    if (cover_band_) {
        lv_obj_clear_flag(cover_band_, LV_OBJ_FLAG_HIDDEN);
    }
    if (cover_accent_) {
        lv_obj_clear_flag(cover_accent_, LV_OBJ_FLAG_HIDDEN);
    }
}

void CoverWidget::renderCover(const BorrowedCover& cover)
{
    if (!cover_img_ || !cover.image) {
        renderPlaceholder();
        return;
    }

    lv_img_cache_invalidate_src(cover.image);
    lv_img_set_src(cover_img_, cover.image);
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

void CoverWidget::clear()
{
    cover_img_ = nullptr;
    cover_band_ = nullptr;
    cover_accent_ = nullptr;
}
