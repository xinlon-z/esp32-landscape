#include "button_mgr.h"

#include "button_state.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_mgr.h"

namespace {

static constexpr gpio_num_t kBootButtonGpio = GPIO_NUM_0;
static constexpr gpio_num_t kPwrButtonGpio = GPIO_NUM_16;
static constexpr int kPollPeriodMs = 20;
static constexpr uint8_t kDebounceTicks = 2;       // 40 ms
static constexpr uint16_t kLongPressTicks = 75;    // 1500 ms

DebouncedButton s_boot(kDebounceTicks, kLongPressTicks);
DebouncedButton s_pwr(kDebounceTicks, kLongPressTicks);
ButtonManagerCallbacks s_callbacks{};

void handleBootEvent(ButtonEvent event)
{
    if (event == ButtonEvent::ShortPress) {
        if (PowerManager::isDisplayOff()) {
            PowerManager::requestWake();
        } else if (s_callbacks.toggle_screen) {
            s_callbacks.toggle_screen();
        }
    } else if (event == ButtonEvent::LongPress) {
        if (PowerManager::isDisplayOff()) {
            PowerManager::requestWake();
        }
        if (s_callbacks.go_home) {
            s_callbacks.go_home();
        }
    }
}

void handlePwrEvent(ButtonEvent event)
{
    if (event == ButtonEvent::ShortPress) {
        if (PowerManager::isDisplayOff()) {
            PowerManager::requestWake();
        } else {
            PowerManager::requestManualSleep();
        }
    } else if (event == ButtonEvent::LongPress) {
        PowerManager::requestPowerOff();
    }
}

void processButtons(bool boot_pressed, bool pwr_pressed)
{
    handleBootEvent(s_boot.update(boot_pressed));
    handlePwrEvent(s_pwr.update(pwr_pressed));
}

void configureButtonGpios()
{
    gpio_config_t cfg = {};
    cfg.intr_type = GPIO_INTR_DISABLE;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pin_bit_mask = (1ULL << kBootButtonGpio) | (1ULL << kPwrButtonGpio);
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&cfg));
}

} // namespace

void ButtonManager::init(const ButtonManagerCallbacks& callbacks)
{
    resetForTest(callbacks);
    configureButtonGpios();
    xTaskCreatePinnedToCore(task, "buttons", 3072, nullptr, 2, nullptr, 1);
}

void ButtonManager::processForTest(bool boot_pressed, bool pwr_pressed)
{
    processButtons(boot_pressed, pwr_pressed);
}

void ButtonManager::resetForTest(const ButtonManagerCallbacks& callbacks)
{
    s_boot.reset();
    s_pwr.reset();
    s_callbacks = callbacks;
}

void ButtonManager::task(void*)
{
    for (;;) {
        const bool boot_pressed = gpio_get_level(kBootButtonGpio) == 0;
        const bool pwr_pressed = gpio_get_level(kPwrButtonGpio) == 0;
        processButtons(boot_pressed, pwr_pressed);
        vTaskDelay(pdMS_TO_TICKS(kPollPeriodMs));
    }
}
