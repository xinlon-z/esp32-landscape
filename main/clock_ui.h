#ifndef CLOCK_UI_H
#define CLOCK_UI_H

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void clock_ui_create(void);
void clock_ui_set_dimmed(bool dimmed);
void clock_ui_set_external_power(bool external_power);

#ifdef __cplusplus
}
#endif

#endif
