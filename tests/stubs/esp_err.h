#pragma once

typedef int esp_err_t;

static constexpr esp_err_t ESP_OK = 0;

#define ESP_ERROR_CHECK_WITHOUT_ABORT(expr) do { (void)(expr); } while (0)
