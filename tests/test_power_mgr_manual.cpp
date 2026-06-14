#include <gtest/gtest.h>
#define private public
#include "platform/power_mgr.cpp"
#undef private

extern "C" esp_err_t adc_get_value(float* value, int* data)
{
    if (value) *value = 0.0f;
    if (data) *data = 0;
    return ESP_OK;
}

namespace ClockNet {
void pauseForSleep() {}
void requestSync() {}
} // namespace ClockNet

class PowerMgrManualTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        s_manual_screen_off.store(false, std::memory_order_relaxed);
        s_activity_seq.store(0, std::memory_order_relaxed);
        s_packed.store(encode(false, 77, false, false, false), std::memory_order_relaxed);
        i2cExioLastPin() = 0xff;
        i2cExioLastLevel() = true;
        lcdBlPrepareDeepSleepCount() = 0;
    }
};

TEST_F(PowerMgrManualTest, ManualSleepAndWakeUpdateStateImmediately)
{
    PowerManager::requestManualSleep();
    PowerManager::State state = PowerManager::getState();
    EXPECT_TRUE(state.dimmed);
    EXPECT_TRUE(state.screen_off);
    EXPECT_FALSE(state.sleeping);
    EXPECT_EQ(state.battery_percent, 77);
    EXPECT_TRUE(s_manual_screen_off.load(std::memory_order_relaxed));

    const uint32_t seq_before_wake = s_activity_seq.load(std::memory_order_relaxed);
    PowerManager::requestWake();
    state = PowerManager::getState();
    EXPECT_FALSE(state.dimmed);
    EXPECT_FALSE(state.screen_off);
    EXPECT_FALSE(state.sleeping);
    EXPECT_FALSE(s_manual_screen_off.load(std::memory_order_relaxed));
    EXPECT_GT(s_activity_seq.load(std::memory_order_relaxed), seq_before_wake);
}

TEST_F(PowerMgrManualTest, NoteActivityIsIgnoredWhileScreenOff)
{
    PowerManager::requestManualSleep();
    ASSERT_TRUE(s_manual_screen_off.load(std::memory_order_relaxed));
    const uint32_t seq_before_touch = s_activity_seq.load(std::memory_order_relaxed);

    PowerManager::noteActivity();
    EXPECT_TRUE(s_manual_screen_off.load(std::memory_order_relaxed));
    EXPECT_TRUE(PowerManager::getState().screen_off);
    EXPECT_FALSE(PowerManager::getState().sleeping);
    EXPECT_EQ(s_activity_seq.load(std::memory_order_relaxed), seq_before_touch);
}

TEST_F(PowerMgrManualTest, PowerOffReleasesBatteryPowerHold)
{
    PowerManager::requestPowerOff();

    EXPECT_EQ(i2cExioLastPin(), 6u);
    EXPECT_FALSE(i2cExioLastLevel());
    EXPECT_TRUE(PowerManager::getState().screen_off);
}

TEST_F(PowerMgrManualTest, DeepSleepPolicyIsBatteryOnlyAfterScreenOffWindow)
{
    EXPECT_FALSE(shouldEnterDeepSleep(false, PowerManager::IdleMode::ScreenOff, 299999));
    EXPECT_TRUE(shouldEnterDeepSleep(false, PowerManager::IdleMode::ScreenOff, 300000));
    EXPECT_TRUE(shouldEnterDeepSleep(false, PowerManager::IdleMode::Sleeping, 0));
    EXPECT_FALSE(shouldEnterDeepSleep(true, PowerManager::IdleMode::ScreenOff, 300000));
    EXPECT_FALSE(shouldEnterDeepSleep(true, PowerManager::IdleMode::Sleeping, 0));
}

TEST_F(PowerMgrManualTest, DimmedBacklightAvoidsIntermediatePwmDuty)
{
    EXPECT_EQ(backlightDutyFor(PowerManager::IdleMode::Active), LCD_PWM_MODE_255);
    EXPECT_EQ(backlightDutyFor(PowerManager::IdleMode::Dimmed), LCD_PWM_MODE_255);
    EXPECT_EQ(backlightDutyFor(PowerManager::IdleMode::ScreenOff), LCD_PWM_MODE_0);
    EXPECT_EQ(backlightDutyFor(PowerManager::IdleMode::Sleeping), LCD_PWM_MODE_0);
}

TEST_F(PowerMgrManualTest, ExternalPowerRequiresStableSamplesBeforeChanging)
{
    bool stable_ext = false;
    bool last_raw_ext = false;
    uint8_t same_count = kExternalPowerDebounceSamples;

    EXPECT_FALSE(updateStableExternalPower(true, &stable_ext, &last_raw_ext, &same_count));
    EXPECT_FALSE(updateStableExternalPower(false, &stable_ext, &last_raw_ext, &same_count));
    EXPECT_FALSE(updateStableExternalPower(true, &stable_ext, &last_raw_ext, &same_count));
    EXPECT_FALSE(updateStableExternalPower(true, &stable_ext, &last_raw_ext, &same_count));
    EXPECT_TRUE(updateStableExternalPower(true, &stable_ext, &last_raw_ext, &same_count));

    EXPECT_TRUE(updateStableExternalPower(false, &stable_ext, &last_raw_ext, &same_count));
    EXPECT_TRUE(updateStableExternalPower(true, &stable_ext, &last_raw_ext, &same_count));
    EXPECT_TRUE(updateStableExternalPower(false, &stable_ext, &last_raw_ext, &same_count));
    EXPECT_TRUE(updateStableExternalPower(false, &stable_ext, &last_raw_ext, &same_count));
    EXPECT_FALSE(updateStableExternalPower(false, &stable_ext, &last_raw_ext, &same_count));
}

TEST_F(PowerMgrManualTest, ScreenOffElapsedHandlesTickZeroStart)
{
    bool screen_off_active = false;
    TickType_t screen_off_since = 0;

    EXPECT_EQ(updateScreenOffElapsedMs(PowerManager::IdleMode::ScreenOff,
                                       0,
                                       &screen_off_active,
                                       &screen_off_since),
              0u);
    EXPECT_TRUE(screen_off_active);
    EXPECT_EQ(screen_off_since, 0u);

    EXPECT_EQ(updateScreenOffElapsedMs(PowerManager::IdleMode::ScreenOff,
                                       pdMS_TO_TICKS(300000),
                                       &screen_off_active,
                                       &screen_off_since),
              300000u);
    EXPECT_TRUE(shouldEnterDeepSleep(false, PowerManager::IdleMode::ScreenOff, 300000));
}

TEST_F(PowerMgrManualTest, EnterDeepSleepHoldsBacklightOffAndUsesPwrLowWakeSource)
{
    espSleepReset();

    enterDeepSleep();

    EXPECT_EQ(espSleepExt1WakeMask(), 1ULL << 16);
    EXPECT_EQ(espSleepExt1WakeMode(), ESP_EXT1_WAKEUP_ANY_LOW);
    EXPECT_EQ(lcdBlPrepareDeepSleepCount(), 1);
    EXPECT_EQ(espDeepSleepStartCount(), 1);
}

TEST_F(PowerMgrManualTest, EnterDeepSleepDoesNotStartIfWakeConfigFails)
{
    espSleepReset();
    espSleepEnableExt1Result() = ESP_FAIL;

    enterDeepSleep();

    EXPECT_EQ(lcdBlPrepareDeepSleepCount(), 0);
    EXPECT_EQ(espDeepSleepStartCount(), 0);
}
