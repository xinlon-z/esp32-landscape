#include "power_mgr.h"

#include <atomic>

#include "adc_bsp.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd_bl_pwm_bsp.h"

namespace {

static const char* kTag           = "power_mgr";
static const int   kDimTimeoutMs  = 20 * 1000;
static const int   kTaskPeriodMs  = 500;
static const int   kBatSampleEvery = 5;  // ADC read every 5 × 500 ms = 2.5 s
static constexpr float kBatEmptyVoltage = 3.30f;
static constexpr float kBatFullVoltage  = 4.125f;

// GPIO_NUM_16 is the SYS_OUT latch line from the power management IC.
// It is read-only and only used for status logging.
static constexpr gpio_num_t kPowerSenseGpio = GPIO_NUM_16;

// Packed atomic state word (see power_mgr.h for encoding description):
//   bits [7:0]  battery_enc: 0 = unknown, 1..101 = percent 0..100
//   bit  [8]    external_power
//   bit  [9]    dimmed
static std::atomic<uint32_t> s_packed{0};

// Monotonic counter incremented by noteActivity() (Core 0).
// The power task detects changes to reset its local dim timer.
static std::atomic<uint32_t> s_activity_seq{0};

static uint32_t encode(bool ext, int bat, bool dim)
{
    const uint8_t bat_enc = (bat < 0) ? 0u : static_cast<uint8_t>(bat + 1);
    return static_cast<uint32_t>(bat_enc)
         | (static_cast<uint32_t>(ext) << 8)
         | (static_cast<uint32_t>(dim) << 9);
}

static PowerManager::State decode(uint32_t raw)
{
    PowerManager::State s;
    const uint8_t bat_enc = static_cast<uint8_t>(raw & 0xFF);
    s.battery_percent = (bat_enc == 0) ? -1 : static_cast<int>(bat_enc) - 1;
    s.external_power  = (raw >> 8) & 1;
    s.dimmed          = (raw >> 9) & 1;
    return s;
}

} // namespace

int PowerManager::voltageToPercent(float v)
{
    if (v <= kBatEmptyVoltage) return 0;
    if (v >= kBatFullVoltage) return 100;
    return static_cast<int>(((v - kBatEmptyVoltage) * 100.0f
                             / (kBatFullVoltage - kBatEmptyVoltage)) + 0.5f);
}

// Reads ADC 8 times, averages, and applies an α=0.07 exponential filter.
// Returns the filtered battery percent, or -1 if no valid readings.
int PowerManager::sampleBattery(float* filtered)
{
    float vsum = 0.0f;
    int   valid = 0;
    for (int i = 0; i < 8; ++i) {
        float v   = 0.0f;
        int   raw = 0;
        adc_get_value(&v, &raw);
        if (v > 0.1f) {
            vsum += v;
            ++valid;
        }
    }
    if (valid == 0) return -1;

    float v = vsum / static_cast<float>(valid);
    int p = voltageToPercent(v);
    if (p < 0) p = 0;
    if (p > 100) p = 100;

    if (*filtered < 0.0f) {
        *filtered = static_cast<float>(p);
    } else {
        *filtered = *filtered * 0.93f + static_cast<float>(p) * 0.07f;
    }

    int result = static_cast<int>(*filtered + 0.5f);
    if (result < 0)   result = 0;
    if (result > 100) result = 100;
    return result;
}

bool PowerManager::checkExternalPower()
{
    return usb_serial_jtag_is_connected();
}

void PowerManager::task(void*)
{
    // Seed battery state from init()'s initial sample for display continuity.
    const State init_state = getState();
    float   bat_filtered  = (init_state.battery_percent >= 0)
                              ? static_cast<float>(init_state.battery_percent)
                              : -1.0f;
    int     bat_percent   = init_state.battery_percent;
    uint8_t bat_tick      = kBatSampleEvery - 1;  // triggers a new sample after one period

    uint32_t  last_seq    = s_activity_seq.load(std::memory_order_relaxed);
    TickType_t last_active = xTaskGetTickCount();

    for (;;) {
        // Detect touch/activity events from Core 0.
        const uint32_t cur_seq = s_activity_seq.load(std::memory_order_relaxed);
        if (cur_seq != last_seq) {
            last_seq    = cur_seq;
            last_active = xTaskGetTickCount();
        }

        // Battery ADC (throttled to every kBatSampleEvery iterations).
        if (++bat_tick >= kBatSampleEvery) {
            bat_tick    = 0;
            bat_percent = sampleBattery(&bat_filtered);
        }

        const bool ext        = checkExternalPower();
        const bool should_dim = !ext &&
            ((xTaskGetTickCount() - last_active) > pdMS_TO_TICKS(kDimTimeoutMs));

        const uint32_t prev = s_packed.load(std::memory_order_relaxed);
        const uint32_t next = encode(ext, bat_percent, should_dim);

        if (next != prev) {
            const bool prev_dim = (prev >> 9) & 1;
            const bool prev_ext = (prev >> 8) & 1;

            if (should_dim != prev_dim) {
                setUpduty(should_dim ? LCD_PWM_MODE_75 : LCD_PWM_MODE_255);
            }
            if (ext != prev_ext) {
                ESP_LOGI(kTag, "Power source: %s, SYS_OUT=%d",
                         ext ? "USB host" : "battery/standalone",
                         gpio_get_level(kPowerSenseGpio));
            }
            s_packed.store(next, std::memory_order_relaxed);
        }

        vTaskDelay(pdMS_TO_TICKS(kTaskPeriodMs));
    }
}

void PowerManager::init()
{
    // Configure SYS_OUT sense GPIO as input with pull-up.
    gpio_config_t cfg = {};
    cfg.intr_type    = GPIO_INTR_DISABLE;
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.pin_bit_mask = 1ULL << kPowerSenseGpio;
    cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&cfg));

    // Immediate battery sample so the first LVGL render shows a real value.
    float bat_f    = -1.0f;
    const int bat  = sampleBattery(&bat_f);
    const bool ext = checkExternalPower();
    ESP_LOGI(kTag, "Initial power: %s, battery=%d%%, SYS_OUT=%d",
             ext ? "USB host" : "battery/standalone",
             bat, gpio_get_level(kPowerSenseGpio));

    s_packed.store(encode(ext, bat, false), std::memory_order_relaxed);

    xTaskCreatePinnedToCore(task, "power_mgr", 4096, nullptr, 2, nullptr, 1);
}

void PowerManager::noteActivity()
{
    s_activity_seq.fetch_add(1, std::memory_order_relaxed);
}

PowerManager::State PowerManager::getState()
{
    return decode(s_packed.load(std::memory_order_relaxed));
}
