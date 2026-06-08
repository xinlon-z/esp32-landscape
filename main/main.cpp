#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "adc_bsp.h"
#include "platform/clock_net.h"
#include "i2c_bsp.h"
#include "i2c_equipment.h"
#include "lcd_bl_pwm_bsp.h"
#include "lvgl.h"
#include "platform/lvgl_port.h"
#include "platform/power_mgr.h"
#include "app/services/mqtt_service.h"
#include "app/screens/screen_manager.h"
#include "platform/touch_drv.h"
#include "user_config.h"

extern "C" void app_main(void)
{
    lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);
    i2c_master_Init();
    adc_bsp_init();
    PowerManager::init();
    i2c_rtc_setup();
    ClockNet::init();
    MqttService::get().init();
    ESP_ERROR_CHECK(LvglPort::init());

    static lv_indev_drv_t indev;
    lv_indev_drv_init(&indev);
    indev.type    = LV_INDEV_TYPE_POINTER;
    indev.read_cb = TouchDriver::readCb;
    // Use LVGL's built-in gesture recognizer, tuned near our edge/center guards.
    indev.gesture_limit = 72;
    indev.gesture_min_velocity = LV_INDEV_DEF_GESTURE_MIN_VELOCITY;
    lv_indev_drv_register(&indev);

    if (LvglPort::Guard g; g) {
        ScreenManager::instance().create();
    }
}
