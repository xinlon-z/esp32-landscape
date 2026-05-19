#define private public
#include "../main/power_mgr.cpp"
#undef private

#include <stdio.h>

extern "C" void adc_get_value(float* value, int* data)
{
    if (value) *value = 0.0f;
    if (data) *data = 0;
}

int main()
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
        const int actual = PowerManager::voltageToPercent(c.voltage);
        if (actual != c.expected_percent) {
            printf("voltageToPercent(%.3f) expected %d, got %d\n",
                   c.voltage, c.expected_percent, actual);
            return 1;
        }
    }

    return 0;
}
