idf_component_register(
  SRCS "lcd_bl_pwm_bsp.c"
  PRIV_REQUIRES driver esp_driver_ledc esp_driver_gpio main                 
  INCLUDE_DIRS "./")
