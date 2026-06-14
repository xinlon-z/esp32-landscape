#include <stdio.h>
#include "lcd_bl_pwm_bsp.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "user_config.h"

static const char* TAG = "lcd_bl";
static const int kBacklightOffLevel = 1;

static esp_err_t backlight_gpio_drive_off(void)
{
  gpio_config_t gpio_conf = {};
  gpio_conf.intr_type = GPIO_INTR_DISABLE;
  gpio_conf.mode = GPIO_MODE_OUTPUT;
  gpio_conf.pin_bit_mask = ((uint64_t)0X01<<EXAMPLE_PIN_NUM_BK_LIGHT);
  gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;

  esp_err_t err = gpio_config(&gpio_conf);
  if (err != ESP_OK) {
    return err;
  }
  return gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, kBacklightOffLevel);
}

static void release_backlight_sleep_hold(void)
{
  esp_err_t err = backlight_gpio_drive_off();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "failed to drive backlight off before hold release: %s", esp_err_to_name(err));
  }
  gpio_deep_sleep_hold_dis();
  err = gpio_hold_dis(EXAMPLE_PIN_NUM_BK_LIGHT);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "failed to release backlight GPIO hold: %s", esp_err_to_name(err));
  }
}

void lcd_bl_pwm_bsp_init(uint16_t duty)
{ 
  release_backlight_sleep_hold();

  ledc_timer_config_t timer_conf = 
  {
    .speed_mode =  LEDC_LOW_SPEED_MODE,
    .duty_resolution = LEDC_TIMER_8_BIT, //256
    .timer_num =  LEDC_TIMER_3,
    .freq_hz = 50 * 1000,
    .clk_cfg = LEDC_SLOW_CLK_RC_FAST,
  };
  ledc_channel_config_t ledc_conf = 
  {
    .gpio_num = EXAMPLE_PIN_NUM_BK_LIGHT,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel =  LEDC_CHANNEL_1,
    .intr_type =  LEDC_INTR_DISABLE,
    .timer_sel = LEDC_TIMER_3,
    .duty = duty,   //占空比
    .hpoint = 0,    //相位
  };
  esp_err_t err = ledc_timer_config(&timer_conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(err));
  }
  err = ledc_channel_config(&ledc_conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "backlight PWM initialized: gpio=%d duty=%u", EXAMPLE_PIN_NUM_BK_LIGHT, duty);
  }
}

void setUpduty(uint16_t duty)
{
  esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "set backlight duty failed: %s", esp_err_to_name(err));
    return;
  }
  err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "update backlight duty failed: %s", esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "backlight duty updated: %u", duty);
  }
}

void lcd_bl_prepare_deep_sleep(void)
{
  esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, LCD_PWM_MODE_0);
  if (err == ESP_OK) {
    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
  }
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "failed to set backlight off before deep sleep: %s", esp_err_to_name(err));
  }

  err = backlight_gpio_drive_off();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "failed to drive backlight GPIO off before deep sleep: %s", esp_err_to_name(err));
    return;
  }

  err = gpio_hold_en(EXAMPLE_PIN_NUM_BK_LIGHT);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "failed to enable backlight GPIO hold: %s", esp_err_to_name(err));
    return;
  }
  gpio_deep_sleep_hold_en();
  ESP_LOGI(TAG, "backlight held off for deep sleep: gpio=%d level=%d",
           EXAMPLE_PIN_NUM_BK_LIGHT, kBacklightOffLevel);
}
