#include "lvgl_port.h"

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
static uint16_t*         s_dma_buf     = nullptr;
static bool              s_initialized = false;

#if (Rotated == USER_DISP_ROT_90)
static uint16_t* s_rot_buf = nullptr;
#endif

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
    esp_lcd_panel_handle_t panel      = static_cast<esp_lcd_panel_handle_t>(drv->user_data);
    const int              flush_cnt  = LVGL_SPIRAM_BUFF_LEN / LVGL_DMA_BUFF_LEN;
    const int              row_step   = LCD_NOROT_VRES / flush_cnt;
    const int              dma_pixels = LVGL_DMA_BUFF_LEN / 2;
    int y1 = 0;
    int y2 = row_step;

#if (Rotated == USER_DISP_ROT_90)
    const uint16_t* src = reinterpret_cast<const uint16_t*>(color_map);
    uint32_t idx = 0;
    for (uint16_t col = 0; col < EXAMPLE_LCD_H_RES; ++col) {
        for (uint16_t row = 0; row < EXAMPLE_LCD_V_RES; ++row) {
            s_rot_buf[idx++] = src[EXAMPLE_LCD_H_RES * (EXAMPLE_LCD_V_RES - row - 1) + col];
        }
    }
    uint16_t* map = s_rot_buf;
#else
    uint16_t* map = reinterpret_cast<uint16_t*>(color_map);
#endif

    xSemaphoreGive(s_flush_semap);
    for (int i = 0; i < flush_cnt; ++i) {
        xSemaphoreTake(s_flush_semap, portMAX_DELAY);
        memcpy(s_dma_buf, map, LVGL_DMA_BUFF_LEN);
        esp_lcd_panel_draw_bitmap(panel, 0, y1, LCD_NOROT_HRES, y2, s_dma_buf);
        y1  += row_step;
        y2  += row_step;
        map += dma_pixels;
    }
    xSemaphoreTake(s_flush_semap, portMAX_DELAY);
    lv_disp_flush_ready(drv);
}

void LvglPort::tickCb(void*)
{
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

void LvglPort::portTask(void*)
{
    for (;;) {
        uint32_t delay = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        if (lock(-1)) {
            delay = lv_timer_handler();
            unlock();
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

    // DMA buffer (internal SRAM, DMA-capable)
    s_dma_buf = static_cast<uint16_t*>(heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA));
    if (!s_dma_buf) return ESP_ERR_NO_MEM;

#if (Rotated == USER_DISP_ROT_90)
    s_rot_buf = static_cast<uint16_t*>(
        heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
    if (!s_rot_buf) return ESP_ERR_NO_MEM;
#endif

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

    BaseType_t created = xTaskCreatePinnedToCore(portTask, "lvgl", 4096, nullptr, 4, nullptr, 0);
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
