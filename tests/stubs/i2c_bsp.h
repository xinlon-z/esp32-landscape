#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef void* i2c_master_dev_handle_t;

extern i2c_master_dev_handle_t disp_touch_dev_handle;

esp_err_t i2c_master_touch_write_read(i2c_master_dev_handle_t dev_handle,
                                      const uint8_t* writeBuf,
                                      size_t writeLen,
                                      uint8_t* readBuf,
                                      size_t readLen);

inline uint8_t& i2cExioLastPin()
{
    static uint8_t pin = 0xff;
    return pin;
}

inline bool& i2cExioLastLevel()
{
    static bool level = false;
    return level;
}

static inline esp_err_t i2c_exio_set_output(uint8_t pin, bool level)
{
    i2cExioLastPin() = pin;
    i2cExioLastLevel() = level;
    return ESP_OK;
}
