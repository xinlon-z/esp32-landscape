#include "touch_drv.h"

#include <stdint.h>

#include "esp_err.h"
#include "i2c_bsp.h"
#include "power_mgr.h"
#include "platform/touch_frame_filter.h"
#include "user_config.h"

namespace {
// The AXS15231B touch driver reports one point as an 8-byte frame.
// Reading a longer frame can leave stale bytes that look like ghost touches.
TouchFrameFilter s_touch_filter;
constexpr uint8_t kTouchCmdLen = 11;
constexpr uint8_t kMaxTouchPoints = 1;
constexpr uint8_t kTouchDataLen = kMaxTouchPoints * 6 + 2;

void mapToLvglPoint(uint16_t raw_x, uint16_t raw_y, lv_point_t* point)
{
#if (Rotated == USER_DISP_ROT_NONO)
    if (raw_x > EXAMPLE_LCD_V_RES) raw_x = EXAMPLE_LCD_V_RES;
    if (raw_y > EXAMPLE_LCD_H_RES) raw_y = EXAMPLE_LCD_H_RES;
    point->x = static_cast<lv_coord_t>(raw_y);
    point->y = static_cast<lv_coord_t>(EXAMPLE_LCD_V_RES - raw_x);
#else
    if (raw_x > EXAMPLE_LCD_H_RES) raw_x = EXAMPLE_LCD_H_RES;
    if (raw_y > EXAMPLE_LCD_V_RES) raw_y = EXAMPLE_LCD_V_RES;
    point->x = static_cast<lv_coord_t>(EXAMPLE_LCD_H_RES - raw_x);
    point->y = static_cast<lv_coord_t>(EXAMPLE_LCD_V_RES - raw_y);
#endif
}
} // namespace

void TouchDriver::readCb(lv_indev_drv_t*, lv_indev_data_t* data)
{
    uint8_t cmd[kTouchCmdLen] = {
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
        s_touch_filter.reset();
        return;
    }

    const TouchFrameResult touch = s_touch_filter.process(buf);
    if (touch.state == TouchFrameState::Pressed) {
        data->state = LV_INDEV_STATE_PR;
        PowerManager::noteActivity();
        mapToLvglPoint(touch.x, touch.y, &data->point);
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}
