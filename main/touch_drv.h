#pragma once

#include "lvgl.h"

// Touch controller driver for the AXS15231B panel's integrated capacitive touch.
// readCb() is registered as the LVGL input device callback; it runs on Core 0
// inside the LVGL task context.
namespace TouchDriver {
    void readCb(lv_indev_drv_t* drv, lv_indev_data_t* data);
}
