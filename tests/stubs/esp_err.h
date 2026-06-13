#pragma once

typedef int esp_err_t;

static constexpr esp_err_t ESP_OK = 0;
static constexpr esp_err_t ESP_FAIL = -1;
static constexpr esp_err_t ESP_ERR_INVALID_ARG = 0x102;

static inline const char* esp_err_to_name(esp_err_t)
{
    return "ESP_ERR";
}

#define ESP_ERROR_CHECK_WITHOUT_ABORT(expr) do { (void)(expr); } while (0)
