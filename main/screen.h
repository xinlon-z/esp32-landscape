#pragma once

// Abstract base class for all UI screens.
// Both create() and destroy() must be called with the LVGL lock held
// (e.g. inside a LvglPort::Guard block).
class Screen {
public:
    virtual ~Screen() = default;
    virtual void create()  = 0;
    virtual void destroy() = 0;
};
