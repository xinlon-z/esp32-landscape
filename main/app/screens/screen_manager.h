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
    static void onGestureSwitchTimer(lv_timer_t* timer);

    void detachGestureHandler();
    void switchTo(ScreenId target);
    void handleSwipe(SwipeDirection swipe, const SwipeGestureStats& stats);
    void ensureGestureOverlay();
    void updateGestureFeedback(const SwipeGestureProgress& progress);
    void settleGestureFeedback(bool accepted, SwipeDirection direction);
    void clearGestureFeedback();
    void clearGestureOverlayPointers();

    ScreenId current_ = ScreenId::Clock;
    ScreenId pending_screen_ = ScreenId::Clock;
    ClockScreen clock_;
    MusicScreen music_;
    SwipeGestureDetector swipe_detector_;
    lv_obj_t* gesture_root_ = nullptr;
    lv_obj_t* gesture_overlay_ = nullptr;
    lv_obj_t* gesture_pill_ = nullptr;
    lv_obj_t* gesture_arrow_ = nullptr;
    lv_obj_t* gesture_screen_root_ = nullptr;
    lv_timer_t* tick_timer_ = nullptr;
    lv_timer_t* gesture_switch_timer_ = nullptr;
    int16_t gesture_root_shift_ = 0;
    lv_opa_t gesture_feedback_opa_ = LV_OPA_TRANSP;
};
