#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "platform/touch_drv.cpp"

namespace {

std::array<uint8_t, 8> g_touch_frame{};
esp_err_t             g_touch_ret = ESP_OK;
int                   g_touch_reads = 0;
size_t                g_last_write_len = 0;
size_t                g_last_read_len = 0;
int                   g_note_activity_calls = 0;

void setTouchFrame(std::array<uint8_t, 8> frame)
{
    g_touch_frame = frame;
    g_touch_ret = ESP_OK;
    g_touch_reads = 0;
    g_last_write_len = 0;
    g_last_read_len = 0;
    g_note_activity_calls = 0;
}

void resetTouchDriver()
{
    g_touch_ret = ESP_ERR_INVALID_ARG;
    lv_indev_data_t data{};
    TouchDriver::readCb(nullptr, &data);
    setTouchFrame({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
}

} // namespace

i2c_master_dev_handle_t disp_touch_dev_handle = reinterpret_cast<i2c_master_dev_handle_t>(0x1);

void PowerManager::noteActivity()
{
    ++g_note_activity_calls;
}

esp_err_t i2c_master_touch_write_read(i2c_master_dev_handle_t,
                                      const uint8_t*,
                                      size_t writeLen,
                                      uint8_t* readBuf,
                                      size_t readLen)
{
    ++g_touch_reads;
    g_last_write_len = writeLen;
    g_last_read_len = readLen;
    if (g_touch_ret != ESP_OK) {
        return g_touch_ret;
    }
    std::copy_n(g_touch_frame.begin(), std::min(readLen, g_touch_frame.size()), readBuf);
    return ESP_OK;
}

TEST(TouchDriver, ReportsReleaseForCandidateAndPressForConfirmedTouch)
{
    resetTouchDriver();
    setTouchFrame({
        0x00,
        0x01,
        0x01, 0x20,
        0x00, 0x80,
        0x00, 0x00,
    });

    lv_indev_data_t data{};
    TouchDriver::readCb(nullptr, &data);

    EXPECT_EQ(data.state, LV_INDEV_STATE_REL);
    EXPECT_EQ(g_touch_reads, 1);
    EXPECT_EQ(g_last_write_len, 11u);
    EXPECT_EQ(g_last_read_len, 8u);
    EXPECT_EQ(g_note_activity_calls, 0);

    setTouchFrame({
        0x00,
        0x01,
        0x01, 0x10,
        0x00, 0x90,
        0x00, 0x00,
    });

    TouchDriver::readCb(nullptr, &data);

    EXPECT_EQ(data.state, LV_INDEV_STATE_PR);
    EXPECT_EQ(data.point.x, 640 - 0x110);
    EXPECT_EQ(data.point.y, 172 - 0x090);
    EXPECT_EQ(g_note_activity_calls, 1);
}

TEST(TouchDriver, ResetsFilterAfterI2cError)
{
    resetTouchDriver();
    setTouchFrame({0x00, 0x01, 0x01, 0x20, 0x00, 0x80, 0x00, 0x00});
    lv_indev_data_t data{};
    TouchDriver::readCb(nullptr, &data);
    ASSERT_EQ(data.state, LV_INDEV_STATE_REL);

    g_touch_ret = ESP_ERR_INVALID_ARG;
    TouchDriver::readCb(nullptr, &data);
    ASSERT_EQ(data.state, LV_INDEV_STATE_REL);

    setTouchFrame({0x00, 0x01, 0x01, 0x10, 0x00, 0x90, 0x00, 0x00});
    TouchDriver::readCb(nullptr, &data);

    EXPECT_EQ(data.state, LV_INDEV_STATE_REL);
}
