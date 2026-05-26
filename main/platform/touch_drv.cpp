#include "touch_drv.h"

#include <string.h>

#include "esp_err.h"
#include "i2c_bsp.h"
#include "power_mgr.h"
#include "user_config.h"

void TouchDriver::readCb(lv_indev_drv_t*, lv_indev_data_t* data)
{
    // 11-byte command that asks the controller to report touch points.
    uint8_t cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x0e, 0x0, 0x0, 0x0};
    uint8_t buf[32] = {};

    // Use i2c_master_touch_write_read which waits on the port-1 bus handle
    // (touch controller is on I2C port 1, not port 0).
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        i2c_master_touch_write_read(disp_touch_dev_handle, cmd, sizeof(cmd), buf, sizeof(buf)));

    if (buf[1] > 0 && buf[1] < 5) {
        uint16_t x = (static_cast<uint16_t>(buf[2] & 0x0f) << 8) | buf[3];
        uint16_t y = (static_cast<uint16_t>(buf[4] & 0x0f) << 8) | buf[5];

        data->state = LV_INDEV_STATE_PR;
        PowerManager::noteActivity();

#if (Rotated == USER_DISP_ROT_NONO)
        if (x > EXAMPLE_LCD_V_RES) x = EXAMPLE_LCD_V_RES;
        if (y > EXAMPLE_LCD_H_RES) y = EXAMPLE_LCD_H_RES;
        data->point.x = static_cast<lv_coord_t>(y);
        data->point.y = static_cast<lv_coord_t>(EXAMPLE_LCD_V_RES - x);
#else
        if (x > EXAMPLE_LCD_H_RES) x = EXAMPLE_LCD_H_RES;
        if (y > EXAMPLE_LCD_V_RES) y = EXAMPLE_LCD_V_RES;
        data->point.x = static_cast<lv_coord_t>(EXAMPLE_LCD_H_RES - x);
        data->point.y = static_cast<lv_coord_t>(EXAMPLE_LCD_V_RES - y);
#endif
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}
