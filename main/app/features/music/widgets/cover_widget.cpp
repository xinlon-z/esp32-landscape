#include "cover_widget.h"

#include "esp_heap_caps.h"

namespace {
constexpr uint32_t kCoverA = 0xd85c52;
constexpr uint32_t kCoverB = 0x24364b;
constexpr uint32_t kCoverC = 0xe7d4bb;
constexpr int kCoverSize = CoverService::kCoverSize;
constexpr int kCoverDisplaySize = 142;
constexpr uint32_t kCoverFadeMs = 180;

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

lv_color_t* allocCoverPixels()
{
    auto* pixels = static_cast<lv_color_t*>(
        heap_caps_malloc(CoverService::kCoverPixelCount * sizeof(lv_color_t),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!pixels) {
        pixels = static_cast<lv_color_t*>(
            heap_caps_malloc(CoverService::kCoverPixelCount * sizeof(lv_color_t),
                             MALLOC_CAP_8BIT));
    }
    return pixels;
}

void setImageOpacity(void* obj, int32_t value)
{
    lv_obj_set_style_img_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value), 0);
}

void hideAfterFade(lv_anim_t* anim)
{
    auto* obj = static_cast<lv_obj_t*>(anim->var);
    if (obj) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void startOpacityAnim(lv_obj_t* obj, int32_t from, int32_t to,
                      lv_anim_ready_cb_t ready_cb = nullptr)
{
    if (!obj) {
        return;
    }
    lv_anim_del(obj, setImageOpacity);
    setImageOpacity(obj, from);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_exec_cb(&anim, setImageOpacity);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_time(&anim, kCoverFadeMs);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    if (ready_cb) {
        lv_anim_set_ready_cb(&anim, ready_cb);
    }
    lv_anim_start(&anim);
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

    for (CoverSlot& slot : slots_) {
        slot.image_obj = lv_img_create(cover);
        lv_obj_set_size(slot.image_obj, kCoverSize, kCoverSize);
        lv_img_set_zoom(slot.image_obj, (kCoverDisplaySize * LV_IMG_ZOOM_NONE) / kCoverSize);
        lv_obj_center(slot.image_obj);
        lv_obj_set_style_img_opa(slot.image_obj, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(slot.image_obj, LV_OBJ_FLAG_HIDDEN);
    }
    cover_img_ = slots_[0].image_obj;
    active_slot_ = 0;
    has_cover_ = false;
    displayed_cover_id_ = 0;
    ensureBuffers();
}

void CoverWidget::renderPlaceholder()
{
    hideImages();
    if (cover_band_) {
        lv_obj_clear_flag(cover_band_, LV_OBJ_FLAG_HIDDEN);
    }
    if (cover_accent_) {
        lv_obj_clear_flag(cover_accent_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool CoverWidget::renderDisplayCover(RenderedCoverFrame* out_cover)
{
    if (!slots_[0].image_obj || !slots_[1].image_obj) {
        renderPlaceholder();
        return false;
    }
    if (!ensureBuffers()) {
        return currentCover(out_cover);
    }

    const int next_slot = has_cover_ ? 1 - active_slot_ : active_slot_;
    CoverSlot& next = slots_[next_slot];
    lv_img_cache_invalidate_src(&next.image);

    uint32_t cover_id = 0;
    if (!CoverService::get().copyDisplayPixels(next.pixels,
                                               CoverService::kCoverPixelCount,
                                               &next.image,
                                               &cover_id)) {
        return false;
    }

    if (cover_id == displayed_cover_id_) {
        return currentCover(out_cover);
    }
    next.cover_id = cover_id;

    if (cover_band_) {
        lv_obj_add_flag(cover_band_, LV_OBJ_FLAG_HIDDEN);
    }
    if (cover_accent_) {
        lv_obj_add_flag(cover_accent_, LV_OBJ_FLAG_HIDDEN);
    }

    lv_img_set_src(next.image_obj, &next.image);
    lv_img_set_zoom(next.image_obj, (kCoverDisplaySize * LV_IMG_ZOOM_NONE) / kCoverSize);
    lv_obj_center(next.image_obj);
    lv_obj_clear_flag(next.image_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(next.image_obj);
    lv_obj_invalidate(next.image_obj);

    if (has_cover_) {
        CoverSlot& old = slots_[active_slot_];
        startOpacityAnim(next.image_obj, LV_OPA_TRANSP, LV_OPA_COVER);
        startOpacityAnim(old.image_obj, LV_OPA_COVER, LV_OPA_TRANSP, hideAfterFade);
    } else {
        lv_anim_del(next.image_obj, setImageOpacity);
        setImageOpacity(next.image_obj, LV_OPA_COVER);
    }

    active_slot_ = next_slot;
    cover_img_ = next.image_obj;
    has_cover_ = true;
    displayed_cover_id_ = cover_id;
    return currentCover(out_cover);
}

void CoverWidget::clear()
{
    hideImages();
    for (CoverSlot& slot : slots_) {
        lv_img_cache_invalidate_src(&slot.image);
        if (slot.pixels) {
            heap_caps_free(slot.pixels);
        }
        slot = CoverSlot{};
    }
    cover_img_ = nullptr;
    cover_band_ = nullptr;
    cover_accent_ = nullptr;
    active_slot_ = 0;
    has_cover_ = false;
    displayed_cover_id_ = 0;
}

bool CoverWidget::ensureBuffers()
{
    for (CoverSlot& slot : slots_) {
        if (!slot.pixels) {
            slot.pixels = allocCoverPixels();
        }
        if (!slot.pixels) {
            return false;
        }
    }
    return true;
}

bool CoverWidget::currentCover(RenderedCoverFrame* out_cover) const
{
    if (!has_cover_) {
        if (out_cover) {
            *out_cover = RenderedCoverFrame{};
        }
        return false;
    }

    const CoverSlot& active = slots_[active_slot_];
    if (active.cover_id == 0 || !active.pixels || !active.image.data) {
        if (out_cover) {
            *out_cover = RenderedCoverFrame{};
        }
        return false;
    }

    if (out_cover) {
        out_cover->cover_id = active.cover_id;
        out_cover->image = &active.image;
        out_cover->pixels = active.pixels;
    }
    return true;
}

void CoverWidget::hideImages()
{
    for (CoverSlot& slot : slots_) {
        if (!slot.image_obj) {
            continue;
        }
        lv_anim_del(slot.image_obj, setImageOpacity);
        lv_obj_add_flag(slot.image_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_img_opa(slot.image_obj, LV_OPA_TRANSP, 0);
        slot.cover_id = 0;
    }
    has_cover_ = false;
    displayed_cover_id_ = 0;
}
