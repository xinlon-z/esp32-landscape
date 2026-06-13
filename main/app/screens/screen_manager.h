#pragma once

#include "app/screens/clock_screen.h"
#include "app/core/event/app_events.h"
#include "app/screens/gesture_manager.h"
#include "app/screens/music_screen.h"
#include "lvgl.h"

#include <atomic>

class ScreenManager {
public:
    static ScreenManager& instance();

    void create();
    void destroy();
    void tick();
    void attachGestureHandler(lv_obj_t* root);
    void requestButtonAction(ButtonActionId action);

private:
    static void onGestureEvent(lv_event_t* event);
    static void onTickTimer(lv_timer_t* timer);

    void detachGestureHandler();
    void switchTo(ScreenId target);
    void handleSwipe(SwipeDirection swipe);
    void handleButtonAction(ButtonActionId action);
    void ensureGestureOverlay();
    void updateGestureFeedback(const SwipeGestureProgress& progress);
    void clearGestureFeedback();
    void clearGestureOverlayPointers();

    ScreenId current_ = ScreenId::Clock;
    ClockScreen clock_;
    MusicScreen music_;
    SwipeGestureDetector swipe_detector_;
    lv_obj_t* gesture_root_ = nullptr;
    lv_obj_t* gesture_overlay_ = nullptr;
    lv_obj_t* gesture_pill_ = nullptr;
    lv_obj_t* gesture_arrow_slot_ = nullptr;
    lv_obj_t* gesture_arrow_ = nullptr;
    lv_obj_t* gesture_screen_root_ = nullptr;
    lv_timer_t* tick_timer_ = nullptr;
    std::atomic<uint8_t> pending_button_action_{static_cast<uint8_t>(ButtonActionId::None)};
};
