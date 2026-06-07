# ESP32 Landscape Desk Clock

A landscape LVGL application for the Waveshare ESP32-S3-Touch-LCD-3.49 board. The UI is built on a unified MVP + EventBus architecture and includes a desk clock screen and an MQTT-driven music player screen.

![ESP32 landscape desk clock demo](docs/images/esp32-clock-demo.jpg)

## Features

- Landscape 640 × 172 UI for the Waveshare 3.49-inch touch LCD.
- Clock screen with a large seven-segment HH:MM display and blinking seconds colon.
- Status area showing weekday, date, WiFi, NTP sync, USB power, and battery state.
- Music player screen driven by Shairport Sync MQTT metadata.
- Album cover rendering with a dynamic blurred cover background.
- Gesture-based screen navigation between clock and music views.
- WiFi time synchronization with NTP servers:
  - `ntp.ntsc.ac.cn`
  - `ntp.aliyun.com`
  - `ntp.tencent.com`
- RTC update after successful NTP synchronization.
- Battery percentage smoothing to reduce display jitter.
- Battery-friendly dimming after inactivity.
- Screen stays bright while connected to a USB host.

## Hardware

- Waveshare ESP32-S3-Touch-LCD-3.49
- ESP32-S3 with PSRAM
- PCF85063 RTC
- Battery voltage ADC input
- USB Serial/JTAG for flashing and monitoring

Official board documentation: [Waveshare ESP32-S3-Touch-LCD-3.49](https://docs.waveshare.net/ESP32-S3-Touch-LCD-3.49/)

## Pre-built Releases

Ready-to-flash firmware ZIPs are published on the [Releases](../../releases) page for every tagged version. Each archive contains:

- `landscape_clock-merged.bin` — full image (bootloader + partition table + app); use this for first-time flashing.
- `landscape_clock.bin` — application partition only; use this to update without erasing NVS/credentials.
- `bootloader.bin`, `partition-table.bin` — individual components.
- `flash_args` — esptool argument file for scripted flashing.
- `sha256sums.txt` — checksums for all binary files.
- `README.txt` — flashing instructions.

A `.sha256` file for the ZIP itself is also published alongside it for download integrity verification.

## UI Architecture

The app uses a strict MVP + EventBus flow:

```text
Hardware / MQTT / RTC / Power
        |
        v
Services  — hold snapshots and heavy buffers (decoded cover pixels, etc.)
        |
        v
EventBus  — publishes lightweight, trivially-copyable change notifications
        |
        v
Presenter — drains events on Screen::onTick(), computes display state
        |
        v
View      — mutates LVGL widgets on the UI thread only
```

Key rules:

- `EventBus` carries only small, trivially-copyable event structs.
- Services own snapshots and heavy data; presenters never hold raw pixels.
- Presenters are the only UI-layer objects that consume events.
- Views never subscribe to `EventBus`, call services, or own business state.
- `ScreenManager` owns screen lifecycle; each screen owns its Presenter and View.
- All LVGL object mutations happen from the UI tick path only.

## Project Layout

```
main/
  main.cpp                  — firmware bootstrap (power, network, LVGL, services, ScreenManager)
  platform/                 — ESP-IDF / FreeRTOS platform drivers
    lvgl_port.cpp/h         — QSPI LCD, LVGL init, render task (Core 0)
    touch_drv.cpp/h         — AXS15231B touch input
    power_mgr.cpp/h         — battery ADC, USB detect, backlight dimming (Core 1)
    clock_net.cpp/h         — WiFi STA and NTP synchronization
    music_mqtt.cpp/h        — Shairport Sync MQTT transport adapter
  app/
    core/event/             — AppEvent, EventBus, ring-buffer EventQueue
    features/clock/         — Clock MVP: model, presenter, view, seven-segment widget
    features/music/         — Music MVP: state, model, presenter, view, cover/background/
    │                           visualizer widgets
    features/music/util/    — Pure utilities: blur, visualizer, icon geometry, time format
    screens/                — Screen, ScreenManager, screen lifecycle, gesture routing
    services/               — Time, power, network, MQTT state, cover decode, Shairport parser
    ui/fonts/               — LVGL font assets
  assets/                   — Embedded binary assets (NotoSans CJK subset TTF)
  user_config.h             — Hardware pin assignments and display geometry constants
  clock_secrets_example.h   — Template for WiFi and MQTT credentials

components/                 — Board-support code (I2C buses, ADC, backlight PWM, SensorLib)
sim/                        — SDL/LVGL desktop simulator for music UI smoke tests
tests/                      — GoogleTest host-side tests (models, presenters, services, event queue)
docs/superpowers/           — Architecture specs and implementation plans
```

## Local Configuration

WiFi and MQTT credentials are intentionally not committed.

```sh
cp main/clock_secrets_example.h main/clock_secrets.h
```

Edit `main/clock_secrets.h`:

```cpp
constexpr const char *kWifiSsid     = "YOUR_WIFI_SSID";
constexpr const char *kWifiPassword = "YOUR_WIFI_PASSWORD";
constexpr const char *kMqttHost     = "192.168.31.100";
constexpr int         kMqttPort     = 1883;
constexpr const char *kMqttUsername = "mqtt";
constexpr const char *kMqttPassword = "YOUR_MQTT_PASSWORD";
```

`main/clock_secrets.h` is listed in `.gitignore`.

## Build

Requires **ESP-IDF v6.0.1**. Activate the environment using your installation's export script:

```sh
# Standard ESP-IDF installation
. $IDF_PATH/export.sh
```

Build and flash:

```sh
idf.py build
idf.py -p /dev/cu.usbmodem111401 flash
idf.py -p /dev/cu.usbmodem111401 monitor
```

Substitute the correct serial port for your machine. The monitor log shows WiFi status, NTP server selection, synchronization result, and power-source detection.

## Tests

Host-side GoogleTest suite, builds with a standard CMake toolchain (no ESP-IDF required):

```sh
cmake -B build-tests -S tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

19 test cases covering the EventBus, event queue, all MVP models and presenters, services, screen navigation, and UI utilities. The same suite runs in CI on every push and pull request.

## Simulator

Build the SDL-based music UI simulator:

```sh
cmake -S sim -B build-sim
cmake --build build-sim --target music_ui_sim
```

Offline smoke test (headless):

```sh
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
  build-sim/music_ui_sim --offline --run-ms 1200 --screenshot /tmp/music-ui.bmp
```

MQTT live test:

```sh
build-sim/music_ui_sim \
  --mqtt-host 192.168.31.100 \
  --mqtt-user mqtt \
  --mqtt-pass YOUR_MQTT_PASSWORD \
  --run-ms 6500 \
  --screenshot /tmp/music-ui-bg.bmp
```

## CI

Two jobs run in parallel on every push and pull request:

| Job | Runner | What it checks |
|-----|--------|----------------|
| **Build Firmware** | `espressif/idf:v6.0.1` | Firmware compiles cleanly |
| **Host Tests** | `ubuntu-latest` | All 19 GoogleTest cases pass |

Tagged pushes additionally trigger the **Publish Release** job, which uploads a versioned firmware ZIP to the GitHub Releases page.

## Notes

- USB host detection uses the ESP-IDF USB Serial/JTAG connection status. A simple USB charger or power bank may not be detected as a USB host.
- The hardware charging status pin is not exposed to a GPIO on this board, so the UI shows USB host power rather than charger IC state.
- The firmware image is close to the app partition limit because of embedded font and UI assets. Monitor partition headroom when adding more resources.

## License

MIT License. See [LICENSE](LICENSE).
