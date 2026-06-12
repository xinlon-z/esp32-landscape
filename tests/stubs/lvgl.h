#pragma once

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <stdint.h>
#include <string>
#include <vector>

typedef unsigned short lv_color_t;
typedef unsigned char lv_opa_t;

struct lv_font_t {
    int line_height = 0;
    int base_line = 0;
};
struct lv_img_dsc_t;

inline const lv_font_t lv_font_montserrat_16{16, 0};
inline const lv_font_t lv_font_montserrat_20{20, 0};

struct lv_obj_t {
    enum class Kind {
        Object,
        Label,
        Image,
        Bar,
        Screen,
    };

    Kind kind = Kind::Object;
    lv_obj_t* parent = nullptr;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    unsigned int flags = 0;
    std::string text;
    const lv_img_dsc_t* image_src = nullptr;
    const lv_font_t* text_font = nullptr;
    int label_long_mode = 0;
    lv_color_t text_color = 0;
    lv_color_t bg_color = 0;
    lv_color_t indicator_bg_color = 0;
    lv_color_t border_color = 0;
    lv_opa_t text_opa = 255;
    lv_opa_t bg_opa = 0;
    lv_opa_t indicator_bg_opa = 0;
    int bar_min = 0;
    int bar_max = 100;
    int bar_value = 0;
};

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

#define LV_OPA_TRANSP 0
#define LV_OPA_70 179
#define LV_OPA_COVER 255

#define LV_RADIUS_CIRCLE 32767
#define LV_PART_INDICATOR 1
#define LV_ANIM_OFF 0
#define LV_TEXT_ALIGN_LEFT 0
#define LV_TEXT_ALIGN_CENTER 1
#define LV_LABEL_LONG_WRAP 0
#define LV_LABEL_LONG_CLIP 4

#define LV_OBJ_FLAG_SCROLLABLE 0x01
#define LV_OBJ_FLAG_HIDDEN 0x02

#define LV_SYMBOL_WIFI "wifi"
#define LV_SYMBOL_REFRESH "refresh"
#define LV_SYMBOL_OK "ok"
#define LV_SYMBOL_CLOSE "close"
#define LV_SYMBOL_CHARGE "charge"

static inline lv_color_t lv_color_make(unsigned char r, unsigned char g, unsigned char b)
{
    return static_cast<lv_color_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

static inline lv_color_t lv_color_hex(uint32_t color)
{
    return lv_color_make(static_cast<unsigned char>((color >> 16) & 0xff),
                         static_cast<unsigned char>((color >> 8) & 0xff),
                         static_cast<unsigned char>(color & 0xff));
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

inline uint32_t& lvTickGetStubValue()
{
    static uint32_t value = 0;
    return value;
}

static inline uint32_t lv_tick_elaps(uint32_t) { return lvTickElapsStubValue(); }
static inline uint32_t lv_tick_get() { return lvTickGetStubValue(); }

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
    std::vector<lv_obj_t*> objects;

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
        for (auto* obj : objects) {
            delete obj;
        }
        objects.clear();
    }
};

inline LvglStubState& lvglStubState()
{
    static LvglStubState state;
    return state;
}

inline lv_obj_t& lvglStubScreen()
{
    static lv_obj_t screen{lv_obj_t::Kind::Screen};
    return screen;
}

static inline lv_obj_t* lv_scr_act()
{
    return &lvglStubScreen();
}

static inline lv_obj_t* lv_obj_create(lv_obj_t* parent)
{
    auto* obj = new lv_obj_t{};
    obj->parent = parent;
    lvglStubState().objects.push_back(obj);
    return obj;
}

static inline lv_obj_t* lv_label_create(lv_obj_t* parent)
{
    lv_obj_t* obj = lv_obj_create(parent);
    obj->kind = lv_obj_t::Kind::Label;
    return obj;
}

static inline lv_obj_t* lv_img_create(lv_obj_t* parent)
{
    lv_obj_t* obj = lv_obj_create(parent);
    obj->kind = lv_obj_t::Kind::Image;
    return obj;
}

static inline lv_obj_t* lv_bar_create(lv_obj_t* parent)
{
    lv_obj_t* obj = lv_obj_create(parent);
    obj->kind = lv_obj_t::Kind::Bar;
    return obj;
}

static inline void lv_obj_clean(lv_obj_t*)
{
}

static inline void lv_obj_set_size(lv_obj_t* obj, int w, int h)
{
    if (obj) {
        obj->w = w;
        obj->h = h;
    }
}

static inline void lv_obj_set_pos(lv_obj_t* obj, int x, int y)
{
    if (obj) {
        obj->x = x;
        obj->y = y;
    }
}

static inline void lv_obj_clear_flag(lv_obj_t* obj, unsigned int flag)
{
    if (obj) {
        obj->flags &= ~flag;
    }
}

static inline void lv_obj_add_flag(lv_obj_t* obj, unsigned int flag)
{
    if (obj) {
        obj->flags |= flag;
    }
}

static inline void lv_obj_set_style_text_font(lv_obj_t* obj, const lv_font_t* font, int)
{
    if (obj) {
        obj->text_font = font;
    }
}

static inline void lv_obj_set_style_text_color(lv_obj_t* obj, lv_color_t color, int)
{
    if (obj) {
        obj->text_color = color;
    }
}

static inline void lv_obj_set_style_text_letter_space(lv_obj_t*, int, int)
{
}

static inline void lv_obj_set_style_text_align(lv_obj_t*, int, int)
{
}

static inline void lv_obj_set_style_text_opa(lv_obj_t* obj, lv_opa_t opa, int)
{
    if (obj) {
        obj->text_opa = opa;
    }
}

static inline void lv_label_set_long_mode(lv_obj_t* obj, int mode)
{
    if (obj) {
        obj->label_long_mode = mode;
    }
}

static inline int lv_font_get_line_height(const lv_font_t* font)
{
    return font ? font->line_height : 0;
}

static inline void lv_obj_set_style_bg_color(lv_obj_t* obj, lv_color_t color, int part)
{
    if (!obj) {
        return;
    }
    if (part == LV_PART_INDICATOR) {
        obj->indicator_bg_color = color;
    } else {
        obj->bg_color = color;
    }
}

static inline void lv_obj_set_style_bg_opa(lv_obj_t* obj, lv_opa_t opa, int part)
{
    if (!obj) {
        return;
    }
    if (part == LV_PART_INDICATOR) {
        obj->indicator_bg_opa = opa;
    } else {
        obj->bg_opa = opa;
    }
}

static inline void lv_obj_set_style_border_color(lv_obj_t* obj, lv_color_t color, int)
{
    if (obj) {
        obj->border_color = color;
    }
}

static inline void lv_obj_set_style_border_width(lv_obj_t*, int, int)
{
}

static inline void lv_obj_set_style_radius(lv_obj_t*, int, int)
{
}

static inline void lv_obj_set_style_pad_all(lv_obj_t*, int, int)
{
}

static inline void lv_obj_set_style_clip_corner(lv_obj_t*, bool, int)
{
}

static inline void lv_label_set_text(lv_obj_t* obj, const char* text)
{
    if (obj) {
        obj->text = text ? text : "";
    }
}

static inline void lv_label_set_text_fmt(lv_obj_t* obj, const char* fmt, ...)
{
    if (!obj || !fmt) {
        return;
    }

    char buf[64];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    obj->text = buf;
}

static inline void lv_bar_set_range(lv_obj_t* obj, int min, int max)
{
    if (obj) {
        obj->bar_min = min;
        obj->bar_max = max;
    }
}

static inline void lv_bar_set_value(lv_obj_t* obj, int value, int)
{
    if (obj) {
        obj->bar_value = value;
    }
}

static inline void lv_img_set_src(lv_obj_t* obj, const lv_img_dsc_t* src)
{
    if (obj) {
        obj->image_src = src;
    }
}

static inline void lv_obj_move_background(lv_obj_t*)
{
}

static inline void lv_obj_invalidate(lv_obj_t*)
{
}

static inline lv_obj_t* lvglFindLabelByText(const char* text)
{
    const std::string wanted = text ? text : "";
    auto& objects = lvglStubState().objects;
    auto it = std::find_if(objects.rbegin(), objects.rend(), [&](const lv_obj_t* obj) {
        return obj && obj->kind == lv_obj_t::Kind::Label && obj->text == wanted;
    });
    return it == objects.rend() ? nullptr : *it;
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

static inline void lv_timer_del(lv_timer_t* timer)
{
    auto& s = lvglStubState();
    if (s.last_timer == timer) {
        delete s.last_timer;
        s.last_timer = nullptr;
        return;
    }
    delete timer;
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
