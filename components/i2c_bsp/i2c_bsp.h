#ifndef I2C_BSP_H
#define I2C_BSP_H
#include <stdbool.h>
#include <stddef.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

extern i2c_master_dev_handle_t disp_touch_dev_handle;
extern i2c_master_dev_handle_t rtc_dev_handle;
extern i2c_master_dev_handle_t imu_dev_handle;
extern i2c_master_dev_handle_t exio_dev_handle;

#ifdef __cplusplus
extern "C" {
#endif

void i2c_master_Init(void);
esp_err_t i2c_writr_buff(i2c_master_dev_handle_t dev_handle,int reg,const uint8_t *buf,size_t len);
esp_err_t i2c_master_write_read_dev(i2c_master_dev_handle_t dev_handle,const uint8_t *writeBuf,size_t writeLen,uint8_t *readBuf,size_t readLen);
esp_err_t i2c_read_buff(i2c_master_dev_handle_t dev_handle,int reg,uint8_t *buf,size_t len);
esp_err_t i2c_master_touch_write_read(i2c_master_dev_handle_t dev_handle,const uint8_t *writeBuf,size_t writeLen,uint8_t *readBuf,size_t readLen);
esp_err_t i2c_exio_set_output(uint8_t pin, bool level);

#ifdef __cplusplus
}
#endif

#endif
