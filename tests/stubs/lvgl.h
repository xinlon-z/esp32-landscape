#pragma once

#include <stdint.h>

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

static inline unsigned int lv_color_to32(lv_color_t color)
{
    const unsigned int r = ((color >> 11) & 0x1f) << 3;
    const unsigned int g = ((color >> 5) & 0x3f) << 2;
    const unsigned int b = (color & 0x1f) << 3;
    return (r << 16) | (g << 8) | b;
}

inline uint32_t& lvTickElapsStubValue()
{
    static uint32_t value = 0;
    return value;
}

static inline uint32_t lv_tick_elaps(uint32_t) { return lvTickElapsStubValue(); }

// LVGL async / timer / cache stubs. The host harness records these calls so
// tests can assert on dispatch ordering without actually running an LVGL loop.

typedef void (*lv_async_cb_t)(void*);

struct lv_timer_t {
    void (*timer_cb)(lv_timer_t*) = nullptr;
    void* user_data = nullptr;
    uint32_t period = 0;
    int repeat_count = -1;
};

struct LvglStubState {
    int async_calls = 0;
    lv_async_cb_t last_async_cb = nullptr;
    void* last_async_user_data = nullptr;

    int timer_creates = 0;
    int img_cache_invalidations = 0;
    lv_timer_t* last_timer = nullptr;
    bool fail_timer_create = false;

    void reset()
    {
        async_calls = 0;
        last_async_cb = nullptr;
        last_async_user_data = nullptr;
        timer_creates = 0;
        img_cache_invalidations = 0;
        if (last_timer) {
            delete last_timer;
            last_timer = nullptr;
        }
        fail_timer_create = false;
    }
};

inline LvglStubState& lvglStubState()
{
    static LvglStubState state;
    return state;
}

static inline int lv_async_call(lv_async_cb_t cb, void* user_data)
{
    auto& s = lvglStubState();
    s.async_calls++;
    s.last_async_cb = cb;
    s.last_async_user_data = user_data;
    return 0;
}

static inline lv_timer_t* lv_timer_create(void (*cb)(lv_timer_t*), uint32_t period, void* user_data)
{
    auto& s = lvglStubState();
    s.timer_creates++;
    if (s.fail_timer_create) {
        return nullptr;
    }
    auto* timer = new lv_timer_t{};
    timer->timer_cb = cb;
    timer->user_data = user_data;
    timer->period = period;
    if (s.last_timer) {
        delete s.last_timer;
    }
    s.last_timer = timer;
    return timer;
}

static inline void lv_timer_set_repeat_count(lv_timer_t* timer, int count)
{
    if (timer) {
        timer->repeat_count = count;
    }
}

static inline void lv_img_cache_invalidate_src(const lv_img_dsc_t*)
{
    lvglStubState().img_cache_invalidations++;
}
