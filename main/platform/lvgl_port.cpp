#include "lvgl_port.h"
#include "lvgl_rotation.h"
#include "lvgl_task_config.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_axs15231b.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "user_config.h"

namespace {

static const char* kTag = "lvgl_port";

static SemaphoreHandle_t s_mux         = nullptr;
static SemaphoreHandle_t s_flush_semap = nullptr;
// Two ping-pong DMA-capable staging buffers. While the panel DMAs out of
// one, flushCb writes the next chunk's rotated pixels into the other.
static uint16_t*         s_dma_bufs[2] = {nullptr, nullptr};
static bool              s_initialized = false;

static const axs15231b_lcd_init_cmd_t kInitCmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 100},
    {0x29, (uint8_t[]){0x00}, 0, 100},
};

} // namespace

// ISR context — only give semaphore, no LVGL calls.
bool LvglPort::onFlushDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_semap, &woken);
    return false;
}

void LvglPort::flushCb(lv_disp_drv_t* drv, const lv_area_t*, lv_color_t* color_map)
{
    esp_lcd_panel_handle_t panel = static_cast<esp_lcd_panel_handle_t>(drv->user_data);

    const int dma_pixels = LVGL_DMA_BUFF_LEN / 2;

#if (Rotated == USER_DISP_ROT_90)
    // Streaming rotate-and-DMA: rotation of chunk N+1 runs in parallel with
    // DMA of chunk N. Each chunk is cols_per_chunk LVGL columns; after
    // rotation it occupies cols_per_chunk native rows × EXAMPLE_LCD_V_RES
    // native cols of the panel.
    //
    // The AXS15231B silicon on this board does NOT honour MADCTL MV in QSPI
    // mode — empirically tested 2026-05 with corrupted output, matching
    // Waveshare's own reference firmware which always uses software
    // rotation. So this software pipeline is the production path.
    const uint16_t cols_per_chunk = static_cast<uint16_t>(dma_pixels / EXAMPLE_LCD_V_RES);
    const uint16_t* src = reinterpret_cast<const uint16_t*>(color_map);

    auto remainingCols = [](uint16_t start) -> uint16_t {
        const uint16_t left = static_cast<uint16_t>(EXAMPLE_LCD_H_RES - start);
        return left;
    };

    int      buf_idx   = 0;
    uint16_t col_start = 0;
    uint16_t col_count = (cols_per_chunk < remainingCols(col_start))
                             ? cols_per_chunk : remainingCols(col_start);

    // Stage chunk 0 before any DMA starts.
    rotateLandscape90Range(src, s_dma_bufs[buf_idx], EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES,
                           col_start, col_count);

    xSemaphoreGive(s_flush_semap);
    while (col_start < EXAMPLE_LCD_H_RES) {
        xSemaphoreTake(s_flush_semap, portMAX_DELAY);
        esp_lcd_panel_draw_bitmap(panel, 0, col_start, LCD_NOROT_HRES,
                                  col_start + col_count, s_dma_bufs[buf_idx]);

        const uint16_t next_start = static_cast<uint16_t>(col_start + col_count);
        if (next_start < EXAMPLE_LCD_H_RES) {
            // While the panel DMAs the chunk we just submitted, rotate the
            // next chunk into the alternate buffer.
            const int      next_buf   = buf_idx ^ 1;
            const uint16_t next_count = (cols_per_chunk < remainingCols(next_start))
                                            ? cols_per_chunk : remainingCols(next_start);
            rotateLandscape90Range(src, s_dma_bufs[next_buf],
                                   EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES,
                                   next_start, next_count);
            buf_idx   = next_buf;
            col_count = next_count;
        }
        col_start = next_start;
    }
    xSemaphoreTake(s_flush_semap, portMAX_DELAY);
#else
    const int flush_cnt = LVGL_SPIRAM_BUFF_LEN / LVGL_DMA_BUFF_LEN;
    const int row_step  = LCD_NOROT_VRES / flush_cnt;
    uint16_t* map = reinterpret_cast<uint16_t*>(color_map);
    int y1 = 0;
    int y2 = row_step;

    xSemaphoreGive(s_flush_semap);
    for (int i = 0; i < flush_cnt; ++i) {
        xSemaphoreTake(s_flush_semap, portMAX_DELAY);
        memcpy(s_dma_bufs[0], map, LVGL_DMA_BUFF_LEN);
        esp_lcd_panel_draw_bitmap(panel, 0, y1, LCD_NOROT_HRES, y2, s_dma_bufs[0]);
        y1  += row_step;
        y2  += row_step;
        map += dma_pixels;
    }
    xSemaphoreTake(s_flush_semap, portMAX_DELAY);
#endif
    lv_disp_flush_ready(drv);
}

void LvglPort::tickCb(void*)
{
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

void LvglPort::portTask(void*)
{
    TickType_t last_stack_log = 0;
    for (;;) {
        uint32_t delay = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        if (lock(-1)) {
            delay = lv_timer_handler();
            unlock();
        }
        const UBaseType_t stack_high_water_words = uxTaskGetStackHighWaterMark(nullptr);
        const uint32_t stack_high_water_bytes =
            static_cast<uint32_t>(stack_high_water_words) * sizeof(StackType_t);
        const TickType_t now = xTaskGetTickCount();
        if (stack_high_water_bytes < lvglStackWarnBytes()) {
            ESP_LOGW(kTag, "lvgl stack high water: %u bytes", stack_high_water_bytes);
        } else if (now - last_stack_log >= pdMS_TO_TICKS(5000)) {
            ESP_LOGI(kTag, "lvgl stack high water: %u bytes", stack_high_water_bytes);
            last_stack_log = now;
        }
        if (delay > EXAMPLE_LVGL_TASK_MAX_DELAY_MS) delay = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        if (delay < EXAMPLE_LVGL_TASK_MIN_DELAY_MS) delay = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
}

esp_err_t LvglPort::init()
{
    if (s_initialized) {
        return ESP_OK;
    }

    // Two DMA-capable staging buffers (internal SRAM). The flush path
    // ping-pongs between them so each chunk's rotation overlaps with the
    // previous chunk's DMA. Replaces the old single 215 KiB SPIRAM rotation
    // buffer (s_rot_buf) — net savings ~193 KiB SPIRAM.
    s_dma_bufs[0] = static_cast<uint16_t*>(heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA));
    s_dma_bufs[1] = static_cast<uint16_t*>(heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA));
    if (!s_dma_bufs[0] || !s_dma_bufs[1]) return ESP_ERR_NO_MEM;

    s_flush_semap = xSemaphoreCreateBinary();
    if (!s_flush_semap) return ESP_ERR_NO_MEM;

    // LCD reset GPIO
    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type    = GPIO_INTR_DISABLE;
    gpio_conf.mode         = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_LCD_RST;
    gpio_conf.pull_up_en   = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&gpio_conf));

    // QSPI bus
    spi_bus_config_t buscfg = {};
    buscfg.data0_io_num    = EXAMPLE_PIN_NUM_LCD_DATA0;
    buscfg.data1_io_num    = EXAMPLE_PIN_NUM_LCD_DATA1;
    buscfg.sclk_io_num     = EXAMPLE_PIN_NUM_LCD_PCLK;
    buscfg.data2_io_num    = EXAMPLE_PIN_NUM_LCD_DATA2;
    buscfg.data3_io_num    = EXAMPLE_PIN_NUM_LCD_DATA3;
    buscfg.max_transfer_sz = LVGL_DMA_BUFF_LEN;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // Panel IO
    esp_lcd_panel_io_handle_t     panel_io  = nullptr;
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num          = EXAMPLE_PIN_NUM_LCD_CS;
    io_config.dc_gpio_num          = GPIO_NUM_NC;
    io_config.spi_mode             = 3;
    io_config.pclk_hz              = 40 * 1000 * 1000;
    io_config.trans_queue_depth    = 10;
    io_config.on_color_trans_done  = onFlushDone;
    io_config.lcd_cmd_bits         = 32;
    io_config.lcd_param_bits       = 8;
    io_config.flags.quad_mode      = true;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        static_cast<esp_lcd_spi_bus_handle_t>(LCD_HOST), &io_config, &panel_io));

    // AXS15231B panel driver
    axs15231b_vendor_config_t vendor_config = {};
    vendor_config.flags.use_qspi_interface  = 1;
    vendor_config.init_cmds                 = kInitCmds;
    vendor_config.init_cmds_size            = sizeof(kInitCmds) / sizeof(kInitCmds[0]);

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num  = GPIO_NUM_NC;
    panel_config.rgb_ele_order   = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel  = 16;
    panel_config.vendor_config   = &vendor_config;

    esp_lcd_panel_handle_t panel = nullptr;
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(panel_io, &panel_config, &panel));

    // Hardware reset: 1 → 0 → 1
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    // LVGL + double-buffered SPIRAM draw buffers
    lv_init();
    auto* buf1 = static_cast<lv_color_t*>(heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN, MALLOC_CAP_SPIRAM));
    auto* buf2 = static_cast<lv_color_t*>(heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN, MALLOC_CAP_SPIRAM));
    if (!buf1 || !buf2) return ESP_ERR_NO_MEM;

    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res      = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb     = flushCb;
    disp_drv.draw_buf     = &disp_buf;
    disp_drv.full_refresh = 1;
    disp_drv.user_data    = panel;
    lv_disp_drv_register(&disp_drv);

    // LVGL tick timer (fires every EXAMPLE_LVGL_TICK_PERIOD_MS ms)
    esp_timer_create_args_t tick_args = {};
    tick_args.callback = tickCb;
    tick_args.name     = "lvgl_tick";
    esp_timer_handle_t tick_timer = nullptr;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    // LVGL mutex and render task (Core 0, priority 4)
    s_mux = xSemaphoreCreateMutex();
    if (!s_mux) return ESP_ERR_NO_MEM;

    BaseType_t created = xTaskCreatePinnedToCore(portTask, "lvgl",
                                                 lvglTaskStackBytes(),
                                                 nullptr, 4, nullptr, 0);
    if (created != pdPASS) return ESP_FAIL;

    s_initialized = true;
    ESP_LOGI(kTag, "Ready %dx%d", EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
    return ESP_OK;
}

bool LvglPort::lock(int timeout_ms)
{
    const TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_mux, ticks) == pdTRUE;
}

void LvglPort::unlock()
{
    xSemaphoreGive(s_mux);
}
