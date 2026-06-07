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

## Host-side unit tests

Pure-logic modules (presenters, services, blur algorithm, blur service queueing) have GoogleTest-based host tests under `tests/`. They use header stubs in `tests/stubs/` for FreeRTOS, LVGL, ESP-IDF heap, and `LvglPort`, so they compile with plain g++ — no ESP-IDF toolchain required.

```sh
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

When adding a new service or model, add a test in `tests/`, register it in `tests/CMakeLists.txt` via `clock_test(test_name)`, and use the `#include "...cpp"` pattern + a friend test peer class to access internals when needed.

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

The firmware is split into two layers: low-level flat modules (direct hardware drivers) and an MVP application layer under `app/`.

**Low-level flat modules** (called from `app/services/` or `main.cpp` directly):

- **`platform/lvgl_port.h/cpp`** — `LvglPort` class. Owns all LCD hardware init (QSPI bus, AXS15231B panel, reset sequence), LVGL init (double-buffered SPIRAM draw buffers, rotation buffer), tick timer, and render task on Core 0. Exposes `LvglPort::lock()` / `unlock()` and an RAII `LvglPort::Guard`.
- **`platform/touch_drv.h/cpp`** — `TouchDriver::readCb` (LVGL indev callback). Reads touch via `i2c_master_touch_write_read` (I2C port 1 — important: the touch controller is on a *separate* I2C bus from the RTC/IMU). Also calls `PowerManager::noteActivity()` to reset the dim timer.
- **`platform/power_mgr.h/cpp`** — `PowerManager` class on Core 1. Monitors USB power, reads battery ADC (8-sample average → α=0.07 EMA), dims/sleeps backlight after inactivity. Exposes `PowerManager::State` via a single atomic load (`getState()`). `noteActivity()` resets the inactivity timer thread-safely.
- **`platform/clock_net.h/cpp`** — `ClockNet` namespace. WiFi STA + manual NTP over UDP (no `esp_sntp`). After sync, writes time to the RTC via `i2c_rtc_setTime()`. Runs both tasks on Core 0.
- **`platform/music_mqtt.h/cpp`** — `MusicMqtt` namespace. Raw MQTT-over-TCP client for Shairport Sync metadata. Delegates state changes to `MqttService::applyField()` and cover images to `CoverService::acceptJpeg()`. Exposes `MusicMqtt::takeCover()` for `MqttService::pumpPendingCover()`.
- **`app/features/music/util/music_background.h/cpp`** — `musicGenerateBlurredBackground()` utility used by `background_image.cpp`.
- **`app/features/music/util/music_visualizer.h/cpp`** — `musicVisualizerBarHeight()` utility used by `visualizer_widget.cpp`.
- **`user_config.h`** — All hardware pin assignments and display geometry constants (`EXAMPLE_LCD_H_RES=640`, `EXAMPLE_LCD_V_RES=172` when `Rotated=USER_DISP_ROT_90`).

**Application layer** (`app/`):

- **`app/core/event/`** — `EventBus` (pub/sub for `AppEvent`), `EventQueue` (bounded SPSC ring buffer), `AppEvents` (all event types: `ClockTimeChanged`, `PowerStateChanged`, `NetworkStateChanged`, `MusicStateChanged`, `CoverStateChanged`, `FeatureAction`).
- **`app/services/`** — Thin adapters that poll the flat modules, detect changes, and publish `AppEvent`s:
  - `TimeService` — polls RTC, publishes `ClockTimeChanged`
  - `PowerService` — polls `PowerManager`, publishes `PowerStateChanged`
  - `NetworkService` — polls `ClockNet`, publishes `NetworkStateChanged`
  - `MqttService` — receives Shairport fields via `applyField()`, publishes `MusicStateChanged`
  - `CoverService` — decodes JPEG covers, publishes `CoverStateChanged`
- **`app/features/clock/`** — MVP for the clock face: `ClockModel` (subscribes to time/power/network events), `ClockView` (LVGL seven-segment display), `ClockPresenter` (bridges model→view).
- **`app/features/music/`** — MVP for the music player: `MusicModel` (subscribes to music/cover/power events), `MusicView` (album art, visualizer, progress bar), `MusicPresenter`.
- **`app/screens/`** — `ScreenManager` (owns both screens, drives swipe navigation, runs a 1 s tick timer that calls `onTick()` on the active screen), `GestureManager` (swipe direction logic), `ClockScreen` / `MusicScreen` (lifecycle: `onEnter`, `onExit`, `onTick`).

### Component Map (`components/`)

- **`i2c_bsp`** — Two I2C master buses: port 0 (GPIO 47/48) for RTC + IMU; port 1 (GPIO 17/18) for touch. Use `i2c_master_touch_write_read()` for touch and `i2c_writr_buff()` / `i2c_read_buff()` for RTC/IMU — mixing these causes wrong bus waits.
- **`i2c_equipment`** — Wraps `SensorPCF85063` (RTC) and `SensorQMI8658` (IMU) from SensorLib. Provides `i2c_rtc_get()` → `RtcDateTime_t` and `i2c_rtc_setTime()`.
- **`adc_bsp`** — Battery voltage ADC. `adc_get_value(float *voltage, int *raw)`.
- **`lcd_bl_pwm_bsp`** — Backlight PWM via LEDC timer 3, channel 1. `setUpduty(LCD_PWM_MODE_75)` dims; `setUpduty(LCD_PWM_MODE_255)` is full brightness.
- **`SensorLib`** — Third-party library (vendored). Provides `SensorPCF85063.hpp` and `SensorQMI8658.hpp`.

### Display Pipeline

LVGL renders to two full-screen SPIRAM draw buffers (`640×172×2 bytes` each). `LvglPort::flushCb` applies a software 90° rotation (column-major transpose into a third SPIRAM buffer `s_rot_buf`), then DMA-transfers the rotated frame to the panel in `LVGL_SPIRAM_BUFF_LEN / LVGL_DMA_BUFF_LEN` DMA-sized chunks, semaphore-paced by `onFlushDone` (ISR callback). `full_refresh = 1` is required by this panel.

### Adding a New Screen

1. Create `main/app/features/myfeature/` with `MyModel`, `MyView`, `MyPresenter` following the `clock` or `music` feature as a template.
2. Create `main/app/screens/my_screen.h/cpp` implementing `onEnter()`, `onExit()`, and `onTick()`.
3. Add the new `ScreenId` enum value in `app/core/event/app_events.h`.
4. Add `MyScreen my_` member to `ScreenManager`, handle it in `switchTo()` and `tick()`, and add the swipe mapping in `GestureManager` (`nextScreenForSwipe()`).
5. Add all new `.cpp` files to `main/CMakeLists.txt` `SRCS`.
