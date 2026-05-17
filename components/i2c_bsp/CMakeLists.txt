idf_component_register(
  SRCS "i2c_bsp.c"
  REQUIRES driver esp_driver_i2c
  PRIV_REQUIRES main
  INCLUDE_DIRS "./")
