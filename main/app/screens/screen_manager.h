#pragma once

#include "app/screens/clock_screen.h"
#include "app/screens/gesture_manager.h"
#include "app/screens/music_screen.h"
#include "lvgl.h"

class ScreenManager {
public:
    static ScreenManager& instance();

    void create();
    void destroy();
    void tick();
    void attachGestureHandler(lv_obj_t* root);

private:
    static void onGestureEvent(lv_event_t* event);
    static void onTickTimer(lv_timer_t* timer);

    void detachGestureHandler();
    void switchTo(ScreenId target);
    void handleSwipe(SwipeDirection swipe, const SwipeGestureStats& stats);

    ScreenId current_ = ScreenId::Clock;
    ClockScreen clock_;
    MusicScreen music_;
    SwipeGestureDetector swipe_detector_;
    lv_obj_t* gesture_root_ = nullptr;
    lv_timer_t* tick_timer_ = nullptr;
};
