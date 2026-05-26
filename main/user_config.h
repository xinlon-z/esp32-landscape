#ifndef USER_CONFIG_H
#define USER_CONFIG_H

//spi & i2c handle
#define LCD_HOST SPI3_HOST

// touch I2C port
#define Touch_SCL_NUM (GPIO_NUM_18)
#define Touch_SDA_NUM (GPIO_NUM_17)

// touch esp
#define ESP_SCL_NUM (GPIO_NUM_48)
#define ESP_SDA_NUM (GPIO_NUM_47)

//  DISP
#define EXAMPLE_PIN_NUM_LCD_CS     (GPIO_NUM_9) 
#define EXAMPLE_PIN_NUM_LCD_PCLK   (GPIO_NUM_10)
#define EXAMPLE_PIN_NUM_LCD_DATA0  (GPIO_NUM_11)
#define EXAMPLE_PIN_NUM_LCD_DATA1  (GPIO_NUM_12)
#define EXAMPLE_PIN_NUM_LCD_DATA2  (GPIO_NUM_13)
#define EXAMPLE_PIN_NUM_LCD_DATA3  (GPIO_NUM_14)
#define EXAMPLE_PIN_NUM_LCD_RST    (GPIO_NUM_21)
#define EXAMPLE_PIN_NUM_BK_LIGHT   (GPIO_NUM_8) 


#define EXAMPLE_PIN_NUM_TOUCH_ADDR        0x3b
#define EXAMPLE_PIN_NUM_TOUCH_RST         (-1)
#define EXAMPLE_PIN_NUM_TOUCH_INT         (-1)


#define EXAMPLE_LVGL_TICK_PERIOD_MS    5
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 5



/*ADDR*/
#define EXAMPLE_RTC_ADDR 0x51

#define EXAMPLE_IMU_ADDR 0x6b


#define USER_DISP_ROT_90    1
#define USER_DISP_ROT_NONO  0
#define Rotated USER_DISP_ROT_90   //软件实现旋转

// Documented dead-end (investigated 2026-05): drive 90° rotation via the
// AXS15231B's MADCTL MV bit instead of transposing pixels in software. The
// vendor's reference firmware
// (https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49 — examples
// 09_LVGL_V8_Test, 10_LVGL_V9_Test, 11_FactoryProgram) and the official docs
// page (https://docs.waveshare.net/ESP32-S3-Touch-LCD-3.49/) all use software
// rotation only — they never call esp_lcd_panel_swap_xy(). Per the
// AXS15231B datasheet (section 3.2.2.36) the MV bit's effective behavior is
// XOR'd with a vendor-set cr_36_opt[2], and the QSPI draw_bitmap path skips
// RASET, so a one-shot RASET (handled below when the flag is on) is required
// for landscape Y range. Kept here as a probe for anyone who wants to retry
// it on different silicon revisions; default off because it cannot be
// validated against the vendor's known-good path on this exact board.
#define HW_ROTATION_TEST          0
#define HW_ROTATION_MIRROR_X      1
#define HW_ROTATION_MIRROR_Y      0


/*bl test*/
#define Backlight_Testing 0





#if (Rotated == USER_DISP_ROT_NONO)
#define EXAMPLE_LCD_H_RES 172   
#define EXAMPLE_LCD_V_RES 640
#else
#define EXAMPLE_LCD_H_RES 640   
#define EXAMPLE_LCD_V_RES 172
#endif

#define LCD_NOROT_HRES     172
#define LCD_NOROT_VRES     640
#define LVGL_DMA_BUFF_LEN (LCD_NOROT_HRES * 64 * 2)
#define LVGL_SPIRAM_BUFF_LEN (EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * 2)




#endif