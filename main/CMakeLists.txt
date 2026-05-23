idf_component_register(
  SRCS "main.cpp"
       "lvgl_port.cpp"
       "touch_drv.cpp"
       "power_mgr.cpp"
       "clock_face_screen.cpp"
       "clock_net.cpp"
       "screen_nav.cpp"
       "music_state.cpp"
       "music_background.cpp"
       "music_visualizer.cpp"
       "music_mqtt.cpp"
       "music_player_screen.cpp"
       "screen_manager.cpp"
  REQUIRES i2c_bsp i2c_equipment adc_bsp lcd_bl_pwm_bsp
           esp_wifi esp_event esp_netif nvs_flash lwip
           esp_driver_usb_serial_jtag esp_timer esp_lcd driver
           SensorLib esp_lcd_axs15231b
  INCLUDE_DIRS "."
  EMBED_FILES "assets/NotoSansSCSubset.ttf")
