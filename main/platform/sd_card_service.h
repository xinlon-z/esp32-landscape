#pragma once

#include "esp_err.h"

namespace SdCardService {

esp_err_t init();
bool mounted();

} // namespace SdCardService
