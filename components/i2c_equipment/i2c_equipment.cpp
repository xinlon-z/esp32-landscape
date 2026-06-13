#include "i2c_equipment.h"
#include "i2c_bsp.h"
#include "user_config.h"
#include "SensorPCF85063.hpp"

#ifndef RTC_PCF85063_ADDR
#define RTC_PCF85063_ADDR EXAMPLE_RTC_ADDR
#endif

SensorPCF85063 rtc;

bool i2c_dev_Callback(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len, bool writeReg, bool isWrite)
{
    esp_err_t ret = ESP_FAIL;
    int areg = reg;
    i2c_master_dev_handle_t i2c_dev_handle = NULL;
    if(RTC_PCF85063_ADDR==addr)
    i2c_dev_handle = rtc_dev_handle;
    else
    return false;

    if(i2c_dev_handle == NULL || (len > 0 && buf == NULL))
    return false;

    if(isWrite) // 写寄存器
    {
        if(writeReg)
        {
            ret = i2c_writr_buff(i2c_dev_handle,areg,buf,len);
        }
        else
        {
            ret = i2c_writr_buff(i2c_dev_handle,-1,buf,len);
        }
    }
    else
    {
      	if(writeReg)
      	{
      	  	ret = i2c_read_buff(i2c_dev_handle,areg,buf,len);
      	}
      	else
      	{
      	    ret = i2c_read_buff(i2c_dev_handle,-1,buf,len);
      	}
    }
    return (ret == ESP_OK) ? true : false;
}

void i2c_rtc_setup(void)
{
    if(!rtc.begin(i2c_dev_Callback))
    {
        ESP_LOGW("rtc","RTC init failed");
    }
}

void i2c_rtc_setTime(uint16_t year,uint8_t month,uint8_t day,uint8_t hour,uint8_t minute,uint8_t second)
{
  	rtc.setDateTime(year, month, day, hour, minute, second);
}

RtcDateTime_t i2c_rtc_get(void)
{
  	RtcDateTime_t time;
  	RTC_DateTime datetime = rtc.getDateTime();
  	time.year = datetime.getYear();
  	time.month = datetime.getMonth();
  	time.day = datetime.getDay();
  	time.hour = datetime.getHour();
  	time.minute = datetime.getMinute();
  	time.second = datetime.getSecond();
  	time.week = datetime.getWeek();
  	return time;
}
