# Landscape Desk Clock Design

## Goal

Build an ESP-IDF/LVGL desk clock for the Waveshare ESP32-S3-Touch-LCD-3.49, optimized for the device lying flat on a desk.

## Approved UI

Use option A V2 from the browser review:

- Logical screen orientation: `640 x 172`.
- Large `HH:MM` time on the left.
- Bottom status pills under the time: RTC sync, dim timeout, tap/PWR wake.
- Right side column: weekday/date and battery/power state.
- Modern, quiet, light theme with no Chinese runtime labels to avoid large CJK font assets.

## Hardware Behavior

- Use the official AXS15231B QSPI LCD and LVGL V8 display path.
- Keep the official software rotation so LVGL renders in landscape while the physical panel remains `172 x 640`.
- Read PCF85063 RTC over I2C for offline timekeeping.
- Dim the LCD backlight after inactivity to support battery use.
- Use touch activity to wake the display back to normal brightness.
- Preserve the PWR button path from the official battery power example where practical.

## Implementation Approach

Start from `Examples/ESP-IDF/09_LVGL_V8_Test` because it already contains the correct rotated LVGL driver, touch mapping, LCD DMA flush path, and backlight PWM component.

Add the RTC helper from `02_I2C_PCF85063` and replace `lv_demo_widgets()` with a small local `clock_ui` module. The UI module owns LVGL object creation and periodic label refresh; `main.cpp` owns board initialization and input/backlight events.

## Verification

Primary verification is `idf.py build` after activating ESP-IDF with:

```bash
source ~/.espressif/tools/activate_idf_v6.0.1.sh
idf.py build
```
