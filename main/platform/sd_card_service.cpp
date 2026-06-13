#include "sd_card_service.h"

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bsp.h"
#include "sdmmc_cmd.h"
#include "user_config.h"

namespace {

constexpr const char* kTag = "sd_card";
sdmmc_card_t* s_card = nullptr;
bool s_mount_attempted = false;

} // namespace

esp_err_t SdCardService::init()
{
    if (s_card) {
        return ESP_OK;
    }
    if (s_mount_attempted) {
        return ESP_ERR_INVALID_STATE;
    }
    s_mount_attempted = true;

    esp_err_t err = i2c_exio_set_output(SD_CARD_POWER_EXIO_PIN, SD_CARD_POWER_ENABLE_LEVEL);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "SD power enable failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(kTag, "SD power enabled via TCA9554 EXIO%u level=%d",
             SD_CARD_POWER_EXIO_PIN, SD_CARD_POWER_ENABLE_LEVEL ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = SD_CARD_CLK_GPIO;
    slot_config.cmd = SD_CARD_CMD_GPIO;
    slot_config.d0 = SD_CARD_D0_GPIO;
    slot_config.d1 = GPIO_NUM_NC;
    slot_config.d2 = GPIO_NUM_NC;
    slot_config.d3 = GPIO_NUM_NC;
    slot_config.cd = SDMMC_SLOT_NO_CD;
    slot_config.wp = SDMMC_SLOT_NO_WP;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 16 * 1024;

    err = esp_vfs_fat_sdmmc_mount(SD_CARD_MOUNT_POINT,
                                  &host,
                                  &slot_config,
                                  &mount_config,
                                  &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "SD card mount failed: %s", esp_err_to_name(err));
        s_card = nullptr;
        return err;
    }

    ESP_LOGI(kTag, "mounted at %s", SD_CARD_MOUNT_POINT);
    return ESP_OK;
}

bool SdCardService::mounted()
{
    return s_card != nullptr;
}
