#pragma once

#include <stdint.h>

#include "lcd_bl_pwm_bsp.h"

constexpr uint16_t initialBacklightDuty()
{
    return LCD_PWM_MODE_0;
}

constexpr uint16_t activeBacklightDuty()
{
    return LCD_PWM_MODE_255;
}
