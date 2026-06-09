#pragma once

#include "background_image.h"
#include "cover_widget.h"
#include "lvgl.h"

#include <stdint.h>

class BackgroundWidget {
public:
    BackgroundWidget();
    ~BackgroundWidget();

    void create(lv_obj_t* parent);
    void renderCover(const RenderedCoverFrame& cover);
    void renderPlaceholder();
    void setBlurEnabled(bool enabled);
    void clear();

private:
    static void onBlurReady(uint32_t cover_id, void* user_data);
    void applyCachedImage(const lv_img_dsc_t* image);

    lv_obj_t* image_obj_ = nullptr;
    uint32_t requested_cover_id_ = 0;
    uint32_t displayed_cover_id_ = 0;
    bool blur_enabled_ = true;
};
