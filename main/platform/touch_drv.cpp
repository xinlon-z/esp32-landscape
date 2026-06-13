#include "touch_drv.h"

#include <stdint.h>

#include "esp_err.h"
#include "i2c_bsp.h"
#include "power_mgr.h"
#include "user_config.h"

namespace {
// The AXS15231B touch driver reports one point as an 8-byte frame.
// Reading a longer frame can leave stale bytes that look like ghost touches.
constexpr uint8_t kMaxTouchPoints = 1;
constexpr uint8_t kTouchDataLen = kMaxTouchPoints * 6 + 2;
constexpr uint8_t kValidFramesBeforePress = 2;
uint8_t s_valid_frame_count = 0;
} // namespace

void TouchDriver::readCb(lv_indev_drv_t*, lv_indev_data_t* data)
{
    uint8_t cmd[11] = {
        0xb5, 0xab, 0xa5, 0x5a,
        0x0, 0x0, 0x0, kTouchDataLen,
        0x0, 0x0, 0x0,
    };
    uint8_t buf[kTouchDataLen] = {};

    // Use i2c_master_touch_write_read which waits on the port-1 bus handle
    // (touch controller is on I2C port 1, not port 0).
    const esp_err_t ret = static_cast<esp_err_t>(
        i2c_master_touch_write_read(disp_touch_dev_handle, cmd, sizeof(cmd), buf, sizeof(buf)));
    if (ret != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(ret);
        data->state = LV_INDEV_STATE_REL;
        s_valid_frame_count = 0;
        return;
    }

    const uint8_t point_count = buf[1];
    const bool valid_point_count = point_count > 0 && point_count <= kMaxTouchPoints;
    if (valid_point_count && s_valid_frame_count < kValidFramesBeforePress) {
        ++s_valid_frame_count;
    } else if (!valid_point_count) {
        s_valid_frame_count = 0;
    }

    const bool pressed = valid_point_count && s_valid_frame_count >= kValidFramesBeforePress;
    if (pressed) {
        uint16_t x = (static_cast<uint16_t>(buf[2] & 0x0f) << 8) | buf[3];
        uint16_t y = (static_cast<uint16_t>(buf[4] & 0x0f) << 8) | buf[5];

#if (Rotated == USER_DISP_ROT_NONO)
        if (x > EXAMPLE_LCD_V_RES) x = EXAMPLE_LCD_V_RES;
        if (y > EXAMPLE_LCD_H_RES) y = EXAMPLE_LCD_H_RES;
#else
        if (x > EXAMPLE_LCD_H_RES) x = EXAMPLE_LCD_H_RES;
        if (y > EXAMPLE_LCD_V_RES) y = EXAMPLE_LCD_V_RES;
#endif

        data->state = LV_INDEV_STATE_PR;
        PowerManager::noteActivity();

#if (Rotated == USER_DISP_ROT_NONO)
        data->point.x = static_cast<lv_coord_t>(y);
        data->point.y = static_cast<lv_coord_t>(EXAMPLE_LCD_V_RES - x);
#else
        data->point.x = static_cast<lv_coord_t>(EXAMPLE_LCD_H_RES - x);
        data->point.y = static_cast<lv_coord_t>(EXAMPLE_LCD_V_RES - y);
#endif
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}
