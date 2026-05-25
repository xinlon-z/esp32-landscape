#pragma once

#include "app/services/cover_service.h"
#include "lvgl.h"

class CoverWidget {
public:
    void create(lv_obj_t* parent);
    void renderPlaceholder();
    void renderCover(const BorrowedCover& cover);
    void clear();

private:
    lv_obj_t* cover_img_ = nullptr;
    lv_obj_t* cover_band_ = nullptr;
    lv_obj_t* cover_accent_ = nullptr;
};
