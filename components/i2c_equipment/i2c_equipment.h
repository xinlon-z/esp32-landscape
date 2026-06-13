#ifndef I2C_EQUIPMENT_H
#define I2C_EQUIPMENT_H

#include <stdint.h>

typedef struct 
{
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t week;
}RtcDateTime_t;

void i2c_rtc_setup(void);
void i2c_rtc_setTime(uint16_t year,uint8_t month,uint8_t day,uint8_t hour,uint8_t minute,uint8_t second);
RtcDateTime_t i2c_rtc_get(void);
#endif 
