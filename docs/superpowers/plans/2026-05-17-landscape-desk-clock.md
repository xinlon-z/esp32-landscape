# Landscape Desk Clock Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a landscape A V2 desk clock firmware for the Waveshare ESP32-S3-Touch-LCD-3.49.

**Architecture:** Use the official LVGL V8 ESP-IDF example as the board support base. Add a focused `clock_ui` module for LVGL layout and refresh, and reuse the official PCF85063 RTC component for offline time.

**Tech Stack:** ESP-IDF v6.0.1, LVGL 8.4, Waveshare AXS15231B panel driver, PCF85063 RTC over I2C.

---

### Task 1: Project Base

**Files:**
- Copy: official `Examples/ESP-IDF/09_LVGL_V8_Test/*` into repo root.

- [ ] Copy the official LVGL V8 ESP-IDF example into the workspace.
- [ ] Keep `Rotated USER_DISP_ROT_90` so LVGL renders `640 x 172`.

### Task 2: RTC And UI Modules

**Files:**
- Copy: `components/SensorLib`
- Copy: `components/i2c_equipment`
- Create: `main/clock_ui.h`
- Create: `main/clock_ui.cpp`
- Modify: `main/CMakeLists.txt`
- Modify: `main/main.cpp`

- [ ] Add PCF85063 access through `i2c_rtc_setup()` and `i2c_rtc_get()`.
- [ ] Create A V2 LVGL objects: large time, status pills, date, battery/power panel.
- [ ] Update labels once per second from RTC.
- [ ] Use placeholder battery percentage until hardware fuel-gauge data is available in the board examples.

### Task 3: Battery-Friendly Backlight

**Files:**
- Modify: `main/main.cpp`
- Use: `components/lcd_bl_pwm_bsp`

- [ ] Track last touch activity.
- [ ] Restore normal brightness on touch.
- [ ] Dim backlight after 20 seconds of inactivity.
- [ ] Keep the UI readable in dim mode.

### Task 4: Build Verification

**Files:**
- Build artifacts under `build/`

- [ ] Run `source ~/.espressif/tools/activate_idf_v6.0.1.sh && idf.py build`.
- [ ] Fix compile errors until build succeeds or report the exact blocker.
