# ESP32 Landscape Desk Clock

A modern landscape clock application for the Waveshare ESP32-S3-Touch-LCD-3.49 board. The UI is designed for a device placed flat on a desk, with a large seven-segment time display and a compact icon-based status area.

![ESP32 landscape desk clock demo](docs/images/esp32-clock-demo.jpg)

## Features

- Landscape 640 x 172 clock UI for the Waveshare 3.49-inch touch LCD.
- Large seven-segment HH:MM display with a blinking seconds colon.
- Right-side status area with weekday, date, WiFi, NTP sync, USB power, and battery status.
- WiFi time synchronization with fallback NTP servers:
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
- USB Serial/JTAG connection for flashing and monitoring

Official board documentation: [Waveshare ESP32-S3-Touch-LCD-3.49](https://docs.waveshare.net/ESP32-S3-Touch-LCD-3.49/)

## Project Layout

- `main/clock_ui.cpp` - LVGL clock UI and battery/status rendering.
- `main/clock_net.cpp` - WiFi connection and NTP synchronization.
- `main/main.cpp` - LCD, touch, backlight, power-state, and LVGL task setup.
- `components/` - Board support code and sensor libraries.
- `docs/images/` - README images and project screenshots.

## WiFi Configuration

WiFi credentials are intentionally not committed to the repository.

Create a local secrets file before building:

```sh
cp main/clock_secrets_example.h main/clock_secrets.h
```

Then edit `main/clock_secrets.h`:

```cpp
constexpr const char *kWifiSsid = "YOUR_WIFI_SSID";
constexpr const char *kWifiPassword = "YOUR_WIFI_PASSWORD";
```

`main/clock_secrets.h` is ignored by Git.

## Build

Activate ESP-IDF v6.0.1:

```sh
source ~/.espressif/tools/activate_idf_v6.0.1.sh
```

Build the firmware:

```sh
idf.py build
```

## Flash

Connect the board over USB, then flash:

```sh
idf.py -p /dev/cu.usbmodem111401 flash
```

Use the correct serial port for your machine if it differs.

## Monitor

```sh
idf.py -p /dev/cu.usbmodem111401 monitor
```

The monitor log shows WiFi connection status, selected NTP server, synchronization result, and power-source detection.

## Notes

- USB host detection uses ESP-IDF USB Serial/JTAG connection status. A simple USB charger or power bank may not be detectable as a USB host.
- The hardware charging status pin is not exposed to an ESP32 GPIO on this board, so the UI indicates USB host power rather than charger IC state.

## License

This project is released under the MIT License. See [LICENSE](LICENSE).
