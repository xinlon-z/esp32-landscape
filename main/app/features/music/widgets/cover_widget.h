#pragma once

#include "app/services/cover_service.h"
#include "lvgl.h"

struct RenderedCoverFrame {
    uint32_t cover_id = 0;
    const lv_img_dsc_t* image = nullptr;
    const lv_color_t* pixels = nullptr;
};

class CoverWidget {
public:
    void create(lv_obj_t* parent);
    void renderPlaceholder();
    bool renderDisplayCover(RenderedCoverFrame* out_cover);
    void clear();

private:
    struct CoverSlot {
        lv_obj_t* image_obj = nullptr;
        lv_color_t* pixels = nullptr;
        lv_img_dsc_t image{};
        uint32_t cover_id = 0;
    };

    bool ensureBuffers();
    bool currentCover(RenderedCoverFrame* out_cover) const;
    void hideImages();

    lv_obj_t* cover_img_ = nullptr;
    lv_obj_t* cover_band_ = nullptr;
    lv_obj_t* cover_accent_ = nullptr;
    CoverSlot slots_[2]{};
    int active_slot_ = 0;
    bool has_cover_ = false;
    uint32_t displayed_cover_id_ = 0;
};
