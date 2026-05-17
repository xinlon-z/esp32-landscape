idf_component_register(
  SRCS "main.cpp" "clock_ui.cpp" "clock_net.cpp"
  REQUIRES i2c_bsp i2c_equipment adc_bsp lcd_bl_pwm_bsp esp_wifi esp_event esp_netif nvs_flash lwip esp_driver_usb_serial_jtag
  INCLUDE_DIRS ".")
