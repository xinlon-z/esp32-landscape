#include <gtest/gtest.h>

#include "platform/backlight_policy.h"

TEST(BacklightPolicy, StartsOffAndTurnsOnAfterUiReady)
{
    EXPECT_EQ(initialBacklightDuty(), LCD_PWM_MODE_0);
    EXPECT_EQ(activeBacklightDuty(), LCD_PWM_MODE_255);
}
