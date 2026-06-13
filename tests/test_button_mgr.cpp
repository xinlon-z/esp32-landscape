#include <gtest/gtest.h>

#include "platform/button_mgr.h"
#include "../main/platform/power_mgr.h"

namespace {

bool g_sleeping = false;
bool g_display_off = false;
int g_wake_count = 0;
int g_manual_sleep_count = 0;
int g_power_off_count = 0;
int g_toggle_count = 0;
int g_home_count = 0;

void toggleScreen()
{
    ++g_toggle_count;
}

void goHome()
{
    ++g_home_count;
}

void resetState(bool sleeping = false)
{
    g_sleeping = sleeping;
    g_display_off = sleeping;
    g_wake_count = 0;
    g_manual_sleep_count = 0;
    g_power_off_count = 0;
    g_toggle_count = 0;
    g_home_count = 0;
    ButtonManager::resetForTest(ButtonManagerCallbacks{toggleScreen, goHome});
}

void releaseButtons()
{
    ButtonManager::processForTest(false, false);
    ButtonManager::processForTest(false, false);
}

void shortPressBoot()
{
    ButtonManager::processForTest(true, false);
    ButtonManager::processForTest(true, false);
    releaseButtons();
}

void shortPressPwr()
{
    ButtonManager::processForTest(false, true);
    ButtonManager::processForTest(false, true);
    releaseButtons();
}

void longPressPwr()
{
    for (int i = 0; i < 80; ++i) {
        ButtonManager::processForTest(false, true);
    }
    releaseButtons();
}

void longPressBoot()
{
    for (int i = 0; i < 80; ++i) {
        ButtonManager::processForTest(true, false);
    }
    releaseButtons();
}

} // namespace

PowerManager::State PowerManager::getState()
{
    State state{};
    state.sleeping = g_sleeping;
    state.dimmed = g_sleeping || g_display_off;
    state.screen_off = g_display_off;
    return state;
}

bool PowerManager::isSleeping()
{
    return g_sleeping;
}

bool PowerManager::isDisplayOff()
{
    return g_sleeping || g_display_off;
}

void PowerManager::requestWake()
{
    ++g_wake_count;
    g_sleeping = false;
    g_display_off = false;
}

void PowerManager::requestManualSleep()
{
    ++g_manual_sleep_count;
    g_display_off = true;
}

void PowerManager::requestPowerOff()
{
    ++g_power_off_count;
    g_sleeping = true;
}

void PowerManager::noteActivity() {}
void PowerManager::init() {}

TEST(ButtonManager, BootShortPressTogglesScreenWhenAwake)
{
    resetState(false);

    shortPressBoot();

    EXPECT_EQ(g_toggle_count, 1);
    EXPECT_EQ(g_home_count, 0);
    EXPECT_EQ(g_wake_count, 0);
}

TEST(ButtonManager, BootShortPressOnlyWakesWhenSleeping)
{
    resetState(true);

    shortPressBoot();

    EXPECT_EQ(g_wake_count, 1);
    EXPECT_EQ(g_toggle_count, 0);
    EXPECT_FALSE(g_sleeping);
}

TEST(ButtonManager, BootLongPressGoesHome)
{
    resetState(false);

    longPressBoot();

    EXPECT_EQ(g_home_count, 1);
    EXPECT_EQ(g_toggle_count, 0);
}

TEST(ButtonManager, PwrShortPressSleepsAndWakes)
{
    resetState(false);

    shortPressPwr();

    EXPECT_EQ(g_manual_sleep_count, 1);
    EXPECT_TRUE(g_display_off);
    EXPECT_FALSE(g_sleeping);

    shortPressPwr();

    EXPECT_EQ(g_wake_count, 1);
    EXPECT_FALSE(g_sleeping);
    EXPECT_FALSE(g_display_off);
}

TEST(ButtonManager, PwrLongPressRequestsPowerOffOnlyOnce)
{
    resetState(false);

    longPressPwr();

    EXPECT_EQ(g_power_off_count, 1);
    EXPECT_EQ(g_manual_sleep_count, 0);
    EXPECT_TRUE(g_sleeping);
}
