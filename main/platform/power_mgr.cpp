#include "power_mgr.h"

#include <atomic>

#include "adc_bsp.h"
#include "clock_net.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bsp.h"
#include "lcd_bl_pwm_bsp.h"

namespace {

static const char* kTag           = "power_mgr";
static const int   kDimTimeoutMs  = 5 * 60 * 1000;
static const int   kScreenOffTimeoutMs = 7 * 60 * 1000;
static const int   kDeepSleepAfterScreenOffMs = 5 * 60 * 1000;
static const int   kSleepTimeoutMs = kScreenOffTimeoutMs + kDeepSleepAfterScreenOffMs;
static const int   kTaskPeriodMs  = 500;
static const int   kBatSampleEvery = 5;  // ADC read every 5 × 500 ms = 2.5 s
static constexpr float kBatEmptyVoltage = 3.30f;
static constexpr float kBatFullVoltage  = 4.125f;

// The board's battery power latch is held through TCA9554 EXIO6 (SYS_EN).
// GPIO_NUM_16 is the PWR/SYS_OUT signal used for status, button polling, and wake.
static constexpr uint8_t kPowerHoldExioPin = 6;
static constexpr gpio_num_t kPowerSenseGpio = GPIO_NUM_16;

// Packed atomic state word (see power_mgr.h for encoding description):
//   bits [7:0]  battery_enc: 0 = unknown, 1..101 = percent 0..100
//   bit  [8]    external_power
//   bit  [9]    dimmed
//   bit  [10]   sleeping
//   bit  [11]   screen_off
static std::atomic<uint32_t> s_packed{0};

// Monotonic counter incremented by noteActivity() (Core 0).
// The power task detects changes to reset its local dim timer.
static std::atomic<uint32_t> s_activity_seq{0};
static std::atomic<bool> s_manual_screen_off{false};

static uint32_t encode(bool ext, int bat, bool dim, bool sleeping, bool screen_off)
{
    const uint8_t bat_enc = (bat < 0) ? 0u : static_cast<uint8_t>(bat + 1);
    return static_cast<uint32_t>(bat_enc)
         | (static_cast<uint32_t>(ext) << 8)
         | (static_cast<uint32_t>(dim) << 9)
         | (static_cast<uint32_t>(sleeping) << 10)
         | (static_cast<uint32_t>(screen_off) << 11);
}

static PowerManager::State decode(uint32_t raw)
{
    PowerManager::State s;
    const uint8_t bat_enc = static_cast<uint8_t>(raw & 0xFF);
    s.battery_percent = (bat_enc == 0) ? -1 : static_cast<int>(bat_enc) - 1;
    s.external_power  = (raw >> 8) & 1;
    s.dimmed          = (raw >> 9) & 1;
    s.sleeping        = (raw >> 10) & 1;
    s.screen_off      = (raw >> 11) & 1;
    return s;
}

static int backlightDutyFor(PowerManager::IdleMode mode)
{
    switch (mode) {
    case PowerManager::IdleMode::Sleeping:
    case PowerManager::IdleMode::ScreenOff:
        return LCD_PWM_MODE_0;
    case PowerManager::IdleMode::Dimmed:
        return LCD_PWM_MODE_150;
    case PowerManager::IdleMode::Active:
    default:
        return LCD_PWM_MODE_255;
    }
}

static void applyPowerState(bool ext,
                            int bat_percent,
                            PowerManager::IdleMode idle_mode,
                            uint32_t idle_ms)
{
    const bool should_sleep = idle_mode == PowerManager::IdleMode::Sleeping;
    const bool should_screen_off = should_sleep || idle_mode == PowerManager::IdleMode::ScreenOff;
    const bool should_dim = should_sleep
                         || idle_mode == PowerManager::IdleMode::Dimmed
                         || idle_mode == PowerManager::IdleMode::ScreenOff;

    const uint32_t prev = s_packed.load(std::memory_order_relaxed);
    const uint32_t next = encode(ext, bat_percent, should_dim, should_sleep, should_screen_off);
    if (next == prev) {
        return;
    }

    const bool prev_dim = (prev >> 9) & 1;
    const bool prev_sleep = (prev >> 10) & 1;
    const bool prev_screen_off = (prev >> 11) & 1;
    const bool prev_ext = (prev >> 8) & 1;

    if (should_dim != prev_dim || should_sleep != prev_sleep || should_screen_off != prev_screen_off) {
        setUpduty(backlightDutyFor(idle_mode));
    }
    if (should_sleep != prev_sleep) {
        if (should_sleep) {
            ESP_LOGI(kTag, "Entering app sleep after %u ms idle", static_cast<unsigned>(idle_ms));
            ClockNet::pauseForSleep();
        } else {
            ESP_LOGI(kTag, "Waking from app sleep");
            ClockNet::requestSync();
        }
    }
    if (ext != prev_ext) {
        ESP_LOGI(kTag, "Power source: %s, SYS_OUT=%d",
                 ext ? "USB host" : "battery/standalone",
                 gpio_get_level(kPowerSenseGpio));
    }
    s_packed.store(next, std::memory_order_relaxed);
}

static void enableBatteryPowerHold()
{
    const esp_err_t err = i2c_exio_set_output(kPowerHoldExioPin, true);
    if (err == ESP_OK) {
        ESP_LOGI(kTag, "Battery power hold enabled via TCA9554 EXIO%u", kPowerHoldExioPin);
    } else {
        ESP_LOGW(kTag, "Failed to enable battery power hold on TCA9554 EXIO%u: %s",
                 kPowerHoldExioPin, esp_err_to_name(err));
    }
}

static bool shouldEnterDeepSleep(bool external_power,
                                 PowerManager::IdleMode idle_mode,
                                 uint32_t screen_off_ms)
{
    if (external_power) {
        return false;
    }
    if (idle_mode == PowerManager::IdleMode::Sleeping) {
        return true;
    }
    return idle_mode == PowerManager::IdleMode::ScreenOff
        && screen_off_ms >= kDeepSleepAfterScreenOffMs;
}

static void enterDeepSleep()
{
    ESP_LOGI(kTag, "Entering deep sleep, wake on PWR GPIO%d low", kPowerSenseGpio);
    ClockNet::pauseForSleep();
    const esp_err_t err = esp_sleep_enable_ext1_wakeup_io(
        1ULL << kPowerSenseGpio,
        ESP_EXT1_WAKEUP_ANY_LOW);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to configure deep sleep wakeup on GPIO%d: %s",
                 kPowerSenseGpio, esp_err_to_name(err));
        return;
    }
    esp_deep_sleep_start();
}

static uint32_t updateScreenOffElapsedMs(PowerManager::IdleMode idle_mode,
                                         TickType_t now,
                                         bool* screen_off_active,
                                         TickType_t* screen_off_since)
{
    if (idle_mode != PowerManager::IdleMode::ScreenOff
        && idle_mode != PowerManager::IdleMode::Sleeping) {
        *screen_off_active = false;
        *screen_off_since = 0;
        return 0;
    }

    if (!*screen_off_active) {
        *screen_off_active = true;
        *screen_off_since = now;
        return 0;
    }

    return static_cast<uint32_t>((now - *screen_off_since) * portTICK_PERIOD_MS);
}

static void forceActivity()
{
    s_activity_seq.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

PowerManager::IdleMode PowerManager::computeIdleMode(bool external_power, uint32_t idle_ms)
{
    if (external_power) {
        return IdleMode::Active;
    }
    if (idle_ms >= kSleepTimeoutMs) {
        return IdleMode::Sleeping;
    }
    if (idle_ms >= kScreenOffTimeoutMs) {
        return IdleMode::ScreenOff;
    }
    if (idle_ms >= kDimTimeoutMs) {
        return IdleMode::Dimmed;
    }
    return IdleMode::Active;
}

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
        if (adc_get_value(&v, &raw) == ESP_OK && v > 0.1f) {
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
    bool screen_off_active = false;
    TickType_t screen_off_since = 0;

    for (;;) {
        const TickType_t now = xTaskGetTickCount();

        // Detect touch/activity events from Core 0.
        const uint32_t cur_seq = s_activity_seq.load(std::memory_order_relaxed);
        if (cur_seq != last_seq) {
            last_seq    = cur_seq;
            last_active = now;
        }

        // Battery ADC (throttled to every kBatSampleEvery iterations).
        if (++bat_tick >= kBatSampleEvery) {
            bat_tick    = 0;
            bat_percent = sampleBattery(&bat_filtered);
        }

        const bool ext = checkExternalPower();
        const uint32_t idle_ms = static_cast<uint32_t>(
            (now - last_active) * portTICK_PERIOD_MS);
        const IdleMode idle_mode = s_manual_screen_off.load(std::memory_order_relaxed)
                                       ? IdleMode::ScreenOff
                                       : computeIdleMode(ext, idle_ms);
        const uint32_t screen_off_ms = updateScreenOffElapsedMs(idle_mode,
                                                                now,
                                                                &screen_off_active,
                                                                &screen_off_since);
        applyPowerState(ext, bat_percent, idle_mode, idle_ms);
        if (shouldEnterDeepSleep(ext, idle_mode, screen_off_ms)) {
            enterDeepSleep();
        }

        vTaskDelay(pdMS_TO_TICKS(kTaskPeriodMs));
    }
}

void PowerManager::init()
{
    enableBatteryPowerHold();

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

    s_packed.store(encode(ext, bat, false, false, false), std::memory_order_relaxed);

    xTaskCreatePinnedToCore(task, "power_mgr", 4096, nullptr, 2, nullptr, 1);
}

void PowerManager::noteActivity()
{
    if (getState().screen_off) {
        return;
    }
    forceActivity();
}

void PowerManager::requestWake()
{
    s_manual_screen_off.store(false, std::memory_order_relaxed);
    forceActivity();
    const State state = getState();
    applyPowerState(state.external_power, state.battery_percent, IdleMode::Active, 0);
}

void PowerManager::requestManualSleep()
{
    s_manual_screen_off.store(true, std::memory_order_relaxed);
    const State state = getState();
    applyPowerState(state.external_power, state.battery_percent, IdleMode::ScreenOff, 0);
}

void PowerManager::requestPowerOff()
{
    requestManualSleep();
    ESP_LOGI(kTag, "Power-off requested via PWR button");
    const esp_err_t err = i2c_exio_set_output(kPowerHoldExioPin, false);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to release battery power hold on TCA9554 EXIO%u: %s",
                 kPowerHoldExioPin, esp_err_to_name(err));
    }
}

bool PowerManager::isSleeping()
{
    return getState().sleeping;
}

bool PowerManager::isDisplayOff()
{
    return getState().screen_off;
}

PowerManager::State PowerManager::getState()
{
    return decode(s_packed.load(std::memory_order_relaxed));
}
