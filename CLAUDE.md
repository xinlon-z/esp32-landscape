# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

ESP-IDF v6.0.1 and toolchain are installed under `/workspace/` (not in `~`):

```sh
# One-time setup per shell session
export IDF_COMPONENT_CACHE_PATH=/workspace/espressif-tools/cache/ComponentManager
source /workspace/activate_idf_v6.0.1.sh

# Build
cd /workspace/ESP32-Clock
idf.py build

# Flash (adjust port)
idf.py -p /dev/cu.usbmodem111401 flash

# Monitor serial output
idf.py -p /dev/cu.usbmodem111401 monitor
```

**Before first build:** copy `main/clock_secrets_example.h` to `main/clock_secrets.h` and fill in WiFi credentials (`kWifiSsid`, `kWifiPassword`).

**Known patch:** `managed_components/espressif__esp_lcd_axs15231b/esp_lcd_axs15231b.c:83` uses `panel_dev_config->rgb_ele_order` (was `color_space` in IDF v5; already patched, but the component manager may overwrite it on `idf.py update-dependencies`).

There are no unit tests — this is firmware; verification is done by building and flashing.

## QEMU Simulation

QEMU for Xtensa (Espressif fork) is installed at `/workspace/espressif-tools/tools/qemu-xtensa/`. Run the simulation with the same environment as the build:

```sh
export IDF_COMPONENT_CACHE_PATH=/workspace/espressif-tools/cache/ComponentManager
source /workspace/activate_idf_v6.0.1.sh
cd /workspace/ESP32-Clock
idf.py qemu
```

`idf.py qemu` automatically merges the bootloader, partition table, and app into `build/qemu_flash.bin` and launches:
```
qemu-system-xtensa -M esp32s3 -m 32M -drive file=qemu_flash.bin,if=mtd,...
    -global driver=timer.esp32s3.timg,property=wdt_disable,value=true
    -nic user,model=open_eth -nographic -serial mon:stdio
```

**What the simulation validates:**
- ✅ ROM bootloader loads the second-stage bootloader correctly
- ✅ Partition table is valid; app image loads from offset `0x10000`
- ✅ Both Xtensa LX7 cores start (`Multicore app`)
- ✅ 30 MB of PSRAM is accessible and self-tests pass
- ✅ FreeRTOS and the full ESP-IDF system layer initialise
- ⏸ Firmware enters `app_main` and then blocks — expected, because QEMU's ESP32-S3 model does not emulate I2C slaves; the `i2c_new_master_bus()` or the first I2C transfer in `i2c_rtc_setup()` never receives a hardware-completion interrupt, causing an indefinite wait

**Known QEMU limitation:** the ESP32-S3 QEMU model does not simulate I2C slave devices (RTC, IMU, touch), QSPI LCD, ADC, or WiFi hardware. The firmware will always stall at `i2c_rtc_setup()` — this is not a firmware bug.

To quit QEMU: press `Ctrl-A` then `x`, or kill the process from another terminal.

## Architecture

The project targets a **Waveshare ESP32-S3-Touch-LCD-3.49** (640×172 landscape display, AXS15231B panel over QSPI, PCF85063 RTC over I2C, QMI8658 IMU over I2C).

### Core Affinity

| Core | Tasks |
|------|-------|
| **Core 0** | LVGL render task (prio 4), WiFi stack (IDF-fixed), NTP sync task |
| **Core 1** | `PowerManager::task` (prio 2) — battery ADC, backlight, power detection |

### Module Map (`main/`)

- **`screen.h`** — Abstract base: `create()` / `destroy()`. All UI screens inherit from this. Both methods must be called with the LVGL lock held.
- **`lvgl_port.h/cpp`** — `LvglPort` class. Owns all LCD hardware init (QSPI bus, AXS15231B panel, reset sequence), LVGL init (double-buffered SPIRAM draw buffers, rotation buffer), tick timer, and render task on Core 0. Exposes `LvglPort::lock()` / `unlock()` and an RAII `LvglPort::Guard`.
- **`touch_drv.h/cpp`** — `TouchDriver::readCb` (LVGL indev callback). Reads touch via `i2c_master_touch_write_read` (I2C port 1 — important: the touch controller is on a *separate* I2C bus from the RTC/IMU).
- **`power_mgr.h/cpp`** — `PowerManager` class on Core 1. Monitors USB power via `usb_serial_jtag_is_connected()`, reads battery ADC (8-sample average → α=0.07 EMA), dims backlight after 20 s of inactivity. Publishes state as a single `std::atomic<uint32_t>` packed word (bits: `[7:0]` battery+1, `[8]` external_power, `[9]` dimmed) for consistent cross-core reads.
- **`clock_face_screen.h/cpp`** — `ClockFaceScreen : Screen`. The landscape seven-segment clock UI. Widget handles are instance member fields (not statics) so multiple instances are safe. A 1-second LVGL timer polls `PowerManager::getState()` and `ClockNet::getStatus()` — there is no push/callback from power manager to UI.
- **`clock_net.h/cpp`** — `ClockNet` namespace. WiFi STA + manual NTP over UDP (no `esp_sntp`). After sync, writes time to the RTC via `i2c_rtc_setTime()`. Runs both tasks on Core 0.
- **`user_config.h`** — All hardware pin assignments and display geometry constants (`EXAMPLE_LCD_H_RES=640`, `EXAMPLE_LCD_V_RES=172` when `Rotated=USER_DISP_ROT_90`).

### Component Map (`components/`)

- **`i2c_bsp`** — Two I2C master buses: port 0 (GPIO 47/48) for RTC + IMU; port 1 (GPIO 17/18) for touch. Use `i2c_master_touch_write_read()` for touch and `i2c_writr_buff()` / `i2c_read_buff()` for RTC/IMU — mixing these causes wrong bus waits.
- **`i2c_equipment`** — Wraps `SensorPCF85063` (RTC) and `SensorQMI8658` (IMU) from SensorLib. Provides `i2c_rtc_get()` → `RtcDateTime_t` and `i2c_rtc_setTime()`.
- **`adc_bsp`** — Battery voltage ADC. `adc_get_value(float *voltage, int *raw)`.
- **`lcd_bl_pwm_bsp`** — Backlight PWM via LEDC timer 3, channel 1. `setUpduty(LCD_PWM_MODE_75)` dims; `setUpduty(LCD_PWM_MODE_255)` is full brightness.
- **`SensorLib`** — Third-party library (vendored). Provides `SensorPCF85063.hpp` and `SensorQMI8658.hpp`.

### Display Pipeline

LVGL renders to two full-screen SPIRAM draw buffers (`640×172×2 bytes` each). `LvglPort::flushCb` applies a software 90° rotation (column-major transpose into a third SPIRAM buffer `s_rot_buf`), then DMA-transfers the rotated frame to the panel in `LVGL_SPIRAM_BUFF_LEN / LVGL_DMA_BUFF_LEN` DMA-sized chunks, semaphore-paced by `onFlushDone` (ISR callback). `full_refresh = 1` is required by this panel.

### Adding a New Screen

1. Create `main/my_screen.h/cpp` with `class MyScreen : public Screen`.
2. Store all LVGL widget handles as member fields, not file-level statics.
3. Register a 1-second `lv_timer_create(onTimer, 1000, this)` in `create()`; delete it in `destroy()`.
4. In `main.cpp`, instantiate `static MyScreen s; if (LvglPort::Guard g; g) { s.create(); }`.
5. Add the `.cpp` to `main/CMakeLists.txt` `SRCS`.
