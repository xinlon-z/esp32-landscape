#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "lvgl.h"

// LVGL display port: initialises the QSPI LCD hardware, LVGL library,
// double-buffered draw buffers, tick timer, and a dedicated render task
// pinned to Core 0. Call init() once from app_main before any LVGL APIs.
class LvglPort {
public:
    static esp_err_t init();

    // Returns true if the mutex was acquired, false on timeout.
    // Pass timeout_ms = -1 for portMAX_DELAY.
    static bool lock(int timeout_ms = -1);
    static void unlock();

    // RAII guard. Usage:
    //   if (LvglPort::Guard g; g) { /* LVGL API calls */ }
    struct Guard {
        explicit Guard(int timeout_ms = -1) : held_(lock(timeout_ms)) {}
        ~Guard() { if (held_) unlock(); }
        explicit operator bool() const { return held_; }
        Guard(const Guard&)            = delete;
        Guard& operator=(const Guard&) = delete;
    private:
        bool held_;
    };

private:
    // Invoked in ISR context by the LCD panel driver when DMA transfer is done.
    static bool onFlushDone(esp_lcd_panel_io_handle_t,
                            esp_lcd_panel_io_event_data_t*, void*);
    static void flushCb(lv_disp_drv_t*, const lv_area_t*, lv_color_t*);
    static void tickCb(void*);
    static void portTask(void*);
};
