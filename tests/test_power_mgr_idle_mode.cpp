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

TEST(PowerMgrIdleMode, ComputeIdleMode)
{
    struct Case {
        bool external_power;
        uint32_t idle_ms;
        PowerManager::IdleMode expected;
    };

    const Case cases[] = {
        {false, 29999, PowerManager::IdleMode::Active},
        {false, 30000, PowerManager::IdleMode::Dimmed},
        {false, 299999, PowerManager::IdleMode::Dimmed},
        {false, 300000, PowerManager::IdleMode::Sleeping},
        {true,  300000, PowerManager::IdleMode::Active},
        {false, 0,      PowerManager::IdleMode::Active},
    };

    for (const Case& c : cases) {
        EXPECT_EQ(PowerManager::computeIdleMode(c.external_power, c.idle_ms), c.expected)
            << "computeIdleMode(ext=" << c.external_power << ", idle=" << static_cast<unsigned>(c.idle_ms) << ")";
    }
}
