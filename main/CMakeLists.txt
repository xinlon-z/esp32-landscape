idf_component_register(
  SRCS "main.cpp"
       "lvgl_port.cpp"
       "touch_drv.cpp"
       "power_mgr.cpp"
       "clock_face_screen.cpp"
       "clock_net.cpp"
       "music_state.cpp"
       "music_background.cpp"
       "music_visualizer.cpp"
       "music_mqtt.cpp"
       "material_status_icons_20.c"
       "music_player_screen.cpp"
       "screen_manager.cpp"
       "app/core/event/event_bus.cpp"
       "app/features/clock/clock_model.cpp"
       "app/features/clock/clock_presenter.cpp"
       "app/features/clock/clock_view.cpp"
       "app/features/clock/widgets/seven_segment_widget.cpp"
       "app/screens/gesture_manager.cpp"
       "app/screens/clock_screen.cpp"
       "app/services/time_service.cpp"
       "app/services/power_service.cpp"
       "app/services/network_service.cpp"
  REQUIRES i2c_bsp i2c_equipment adc_bsp lcd_bl_pwm_bsp
           esp_wifi esp_event esp_netif nvs_flash lwip
           esp_driver_usb_serial_jtag esp_timer esp_lcd driver
           SensorLib esp_lcd_axs15231b
  INCLUDE_DIRS "."
  EMBED_FILES "assets/NotoSansSCSubset.ttf")
