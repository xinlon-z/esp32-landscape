#pragma once

static constexpr int LCD_PWM_MODE_0 = 0;
static constexpr int LCD_PWM_MODE_75 = 75;
static constexpr int LCD_PWM_MODE_150 = 150;
static constexpr int LCD_PWM_MODE_255 = 255;

static inline void setUpduty(int)
{
}

inline int& lcdBlPrepareDeepSleepCount()
{
    static int value = 0;
    return value;
}

static inline void lcd_bl_prepare_deep_sleep()
{
    ++lcdBlPrepareDeepSleepCount();
}
