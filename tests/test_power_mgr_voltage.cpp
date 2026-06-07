#include <gtest/gtest.h>
#define private public
#include "platform/power_mgr.cpp"
#undef private

extern "C" void adc_get_value(float* value, int* data)
{
    if (value) *value = 0.0f;
    if (data) *data = 0;
}

namespace ClockNet {
void pauseForSleep() {}
void requestSync() {}
} // namespace ClockNet

TEST(PowerMgrVoltage, VoltageToPercent)
{
    struct Case {
        float voltage;
        int expected_percent;
    };

    const Case cases[] = {
        {3.30f, 0},
        {4.125f, 100},
        {4.146f, 100},
        {4.15f, 100},
        {4.20f, 100},
    };

    for (const Case& c : cases) {
        EXPECT_EQ(PowerManager::voltageToPercent(c.voltage), c.expected_percent)
            << "voltage " << c.voltage;
    }
}
