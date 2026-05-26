#include "background_widget.h"

#include "app/features/music/widgets/background_blur_service.h"
#include "app/services/cover_service.h"

#include "esp_log.h"

namespace {
constexpr int kScreenW = 640;
constexpr int kScreenH = 172;
constexpr const char* kTraceTag = "bg_widget";
} // namespace

BackgroundWidget::BackgroundWidget()
{
    BackgroundBlurService::get().setReadyCallback(onBlurReady, this);
}

BackgroundWidget::~BackgroundWidget()
{
    BackgroundBlurService::get().clearReadyCallback(this);
}

void BackgroundWidget::create(lv_obj_t* parent)
{
    image_obj_ = lv_img_create(parent);
    lv_obj_set_size(image_obj_, kScreenW, kScreenH);
    lv_obj_set_pos(image_obj_, 0, 0);
    lv_obj_set_style_radius(image_obj_, 0, 0);
    lv_obj_set_style_clip_corner(image_obj_, false, 0);
    lv_obj_add_flag(image_obj_, LV_OBJ_FLAG_HIDDEN);
    displayed_cover_id_ = 0;
}

void BackgroundWidget::renderCover(const BorrowedCover& cover)
{
    if (!image_obj_ || cover.cover_id == 0 || !cover.image || !cover.pixels) {
        renderPlaceholder();
        return;
    }
    if (cover.cover_id == displayed_cover_id_) {
        return;
    }

    requested_cover_id_ = cover.cover_id;
    const lv_img_dsc_t* image = nullptr;
    const bool cache_hit = BackgroundBlurService::get().request(cover, kScreenW, kScreenH, &image);
    ESP_LOGI(kTraceTag, "[trace] renderCover cover=%u displayed=%u cache_hit=%d",
             cover.cover_id, displayed_cover_id_, cache_hit ? 1 : 0);
    if (cache_hit && image) {
        applyCachedImage(image);
        displayed_cover_id_ = cover.cover_id;
        return;
    }

    if (displayed_cover_id_ != 0) {
        lv_obj_add_flag(image_obj_, LV_OBJ_FLAG_HIDDEN);
        displayed_cover_id_ = 0;
    }
}

void BackgroundWidget::renderPlaceholder()
{
    requested_cover_id_ = 0;
    displayed_cover_id_ = 0;
    if (image_obj_) {
        lv_obj_add_flag(image_obj_, LV_OBJ_FLAG_HIDDEN);
    }
}

void BackgroundWidget::clear()
{
    image_obj_ = nullptr;
    displayed_cover_id_ = 0;
}

void BackgroundWidget::onBlurReady(uint32_t cover_id, void* user_data)
{
    auto* self = static_cast<BackgroundWidget*>(user_data);
    ESP_LOGI(kTraceTag, "[trace] onBlurReady cover=%u image_obj=%p requested=%u",
             cover_id, self ? self->image_obj_ : nullptr,
             self ? self->requested_cover_id_ : 0);
    if (!self || !self->image_obj_) {
        return;
    }
    if (self->requested_cover_id_ != cover_id) {
        return;
    }
    const lv_img_dsc_t* image = nullptr;
    if (BackgroundBlurService::get().tryGetCached(cover_id, &image) && image) {
        self->applyCachedImage(image);
        self->displayed_cover_id_ = cover_id;
    }
}

void BackgroundWidget::applyCachedImage(const lv_img_dsc_t* image)
{
    if (!image_obj_ || !image) {
        return;
    }
    lv_img_cache_invalidate_src(image);
    lv_img_set_src(image_obj_, image);
    lv_obj_clear_flag(image_obj_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(image_obj_);
    lv_obj_invalidate(image_obj_);
}
