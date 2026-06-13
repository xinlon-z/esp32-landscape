#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "i2c_bsp.h"
#include "user_config.h"
#include "freertos/FreeRTOS.h"

static i2c_master_bus_handle_t user_i2c_port0_handle = NULL;
static i2c_master_bus_handle_t user_i2c_port1_handle = NULL;
i2c_master_dev_handle_t disp_touch_dev_handle = NULL;
i2c_master_dev_handle_t rtc_dev_handle = NULL;
i2c_master_dev_handle_t exio_dev_handle = NULL;


static uint32_t i2c_data_pdMS_TICKS = 0;
static uint32_t i2c_done_pdMS_TICKS = 0;

enum {
  TCA9554_REG_OUTPUT = 0x01,
  TCA9554_REG_CONFIG = 0x03,
};


void i2c_master_Init(void)
{
  i2c_data_pdMS_TICKS = pdMS_TO_TICKS(5000);
  i2c_done_pdMS_TICKS = pdMS_TO_TICKS(1000);
  /*i2c_port 0 init*/
  i2c_master_bus_config_t i2c_bus_config = 
  {
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .i2c_port = I2C_NUM_0,
    .scl_io_num = ESP_SCL_NUM,
    .sda_io_num = ESP_SDA_NUM,
    .glitch_ignore_cnt = 7,
    .flags = {
      .enable_internal_pullup = true,
    },
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &user_i2c_port0_handle));
  i2c_bus_config.scl_io_num = Touch_SCL_NUM;
  i2c_bus_config.sda_io_num = Touch_SDA_NUM;
  i2c_bus_config.i2c_port = I2C_NUM_1;
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &user_i2c_port1_handle));
  
  i2c_device_config_t dev_cfg = 
  {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .scl_speed_hz = 300000,
  };
  dev_cfg.device_address = EXAMPLE_RTC_ADDR;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port0_handle, &dev_cfg, &rtc_dev_handle));

  dev_cfg.device_address = EXAMPLE_EXIO_ADDR;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port0_handle, &dev_cfg, &exio_dev_handle));

  dev_cfg.device_address = EXAMPLE_PIN_NUM_TOUCH_ADDR;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port1_handle, &dev_cfg, &disp_touch_dev_handle));

}

esp_err_t i2c_writr_buff(i2c_master_dev_handle_t dev_handle,int reg,const uint8_t *buf,size_t len)
{
  esp_err_t ret;
  uint8_t *pbuf = NULL;
  if(dev_handle == NULL || (len > 0 && buf == NULL))
  return ESP_ERR_INVALID_ARG;
  ret = i2c_master_bus_wait_all_done(user_i2c_port0_handle,i2c_done_pdMS_TICKS);
  if(ret != ESP_OK)
  return ret;
  if(reg == -1)
  {
    ret = i2c_master_transmit(dev_handle,buf,len,i2c_data_pdMS_TICKS);
  }
  else
  {
    pbuf = (uint8_t*)malloc(len+1);
    if(pbuf == NULL)
    return ESP_ERR_NO_MEM;
    pbuf[0] = reg;
    memcpy(pbuf + 1, buf, len);
    ret = i2c_master_transmit(dev_handle,pbuf,len+1,i2c_data_pdMS_TICKS);
    free(pbuf);
    pbuf = NULL;
  }
  return ret;
}
esp_err_t i2c_master_write_read_dev(i2c_master_dev_handle_t dev_handle,const uint8_t *writeBuf,size_t writeLen,uint8_t *readBuf,size_t readLen)
{
  esp_err_t ret;
  if(dev_handle == NULL || (writeLen > 0 && writeBuf == NULL) || (readLen > 0 && readBuf == NULL))
  return ESP_ERR_INVALID_ARG;
  ret = i2c_master_bus_wait_all_done(user_i2c_port0_handle,i2c_done_pdMS_TICKS);
  if(ret != ESP_OK)
  return ret;
  ret = i2c_master_transmit_receive(dev_handle,writeBuf,writeLen,readBuf,readLen,i2c_data_pdMS_TICKS);
  return ret;
}

esp_err_t i2c_exio_set_output(uint8_t pin, bool level)
{
  if (pin >= 8 || exio_dev_handle == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t output = 0;
  uint8_t config = 0;
  esp_err_t ret = i2c_read_buff(exio_dev_handle, TCA9554_REG_OUTPUT, &output, 1);
  if (ret != ESP_OK) {
    return ret;
  }
  ret = i2c_read_buff(exio_dev_handle, TCA9554_REG_CONFIG, &config, 1);
  if (ret != ESP_OK) {
    return ret;
  }

  const uint8_t mask = (uint8_t)(1u << pin);
  if (level) {
    output |= mask;
  } else {
    output &= (uint8_t)~mask;
  }
  config &= (uint8_t)~mask;

  ret = i2c_writr_buff(exio_dev_handle, TCA9554_REG_OUTPUT, &output, 1);
  if (ret != ESP_OK) {
    return ret;
  }
  return i2c_writr_buff(exio_dev_handle, TCA9554_REG_CONFIG, &config, 1);
}
esp_err_t i2c_read_buff(i2c_master_dev_handle_t dev_handle,int reg,uint8_t *buf,size_t len)
{
  esp_err_t ret;
  uint8_t addr = 0;
  if(dev_handle == NULL || (len > 0 && buf == NULL))
  return ESP_ERR_INVALID_ARG;
  ret = i2c_master_bus_wait_all_done(user_i2c_port0_handle,i2c_done_pdMS_TICKS);
  if(ret != ESP_OK)
  return ret;
  if( reg == -1 )
  {ret = i2c_master_receive(dev_handle, buf,len, i2c_data_pdMS_TICKS);}
  else
  {addr = (uint8_t)reg; ret = i2c_master_transmit_receive(dev_handle,&addr,1,buf,len,i2c_data_pdMS_TICKS);}
  return ret;
}


esp_err_t i2c_master_touch_write_read(i2c_master_dev_handle_t dev_handle,const uint8_t *writeBuf,size_t writeLen,uint8_t *readBuf,size_t readLen)
{
  esp_err_t ret;
  if(dev_handle == NULL || (writeLen > 0 && writeBuf == NULL) || (readLen > 0 && readBuf == NULL))
  return ESP_ERR_INVALID_ARG;
  ret = i2c_master_bus_wait_all_done(user_i2c_port1_handle,i2c_done_pdMS_TICKS);
  if(ret != ESP_OK)
  return ret;
  // AXS15231B touch reads follow Espressif's driver: command write, then a separate data read.
  ret = i2c_master_transmit(dev_handle,writeBuf,writeLen,i2c_data_pdMS_TICKS);
  if(ret != ESP_OK)
  return ret;
  ret = i2c_master_receive(dev_handle,readBuf,readLen,i2c_data_pdMS_TICKS);
  return ret;
}
