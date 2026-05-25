#pragma once

struct lv_obj_t {};
typedef unsigned short lv_color_t;

struct lv_img_header_t {
    unsigned int always_zero = 0;
    unsigned int w = 0;
    unsigned int h = 0;
    unsigned int cf = 0;
};

struct lv_img_dsc_t {
    lv_img_header_t header{};
    const unsigned char* data = nullptr;
    unsigned int data_size = 0;
};

#define LV_IMG_CF_TRUE_COLOR 1

static inline lv_color_t lv_color_make(unsigned char r, unsigned char g, unsigned char b)
{
    return static_cast<lv_color_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}
