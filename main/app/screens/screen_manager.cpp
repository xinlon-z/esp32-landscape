#include "app/screens/screen_manager.h"

#include <stdint.h>

#include "esp_log.h"

namespace {

constexpr const char* kTag = "screen_mgr";

void enableTouchEvents(lv_obj_t* obj, bool bubble_to_parent)
{
    if (!obj) {
        return;
    }

    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    if (bubble_to_parent) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    }

    const uint32_t child_count = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < child_count; ++i) {
        enableTouchEvents(lv_obj_get_child(obj, i), true);
    }
}

TouchPoint currentTouchPoint()
{
    lv_point_t point{};
    lv_indev_t* indev = lv_indev_get_act();
    if (indev) {
        lv_indev_get_point(indev, &point);
    }
    return TouchPoint{static_cast<int16_t>(point.x), static_cast<int16_t>(point.y)};
}

const char* screenName(ScreenId screen)
{
    return screen == ScreenId::Clock ? "clock" : "music";
}

const char* swipeName(SwipeDirection swipe)
{
    if (swipe == SwipeDirection::Left) {
        return "left";
    }
    if (swipe == SwipeDirection::Right) {
        return "right";
    }
    return "none";
}

} // namespace

ScreenManager& ScreenManager::instance()
{
    static ScreenManager manager;
    return manager;
}

void ScreenManager::create()
{
    current_ = ScreenId::Clock;
    clock_.onEnter();
    attachGestureHandler(lv_scr_act());
    tick_timer_ = lv_timer_create(onTickTimer, 1000, this);
}

void ScreenManager::destroy()
{
    if (tick_timer_) {
        lv_timer_del(tick_timer_);
        tick_timer_ = nullptr;
    }

    detachGestureHandler();
    if (current_ == ScreenId::Clock) {
        clock_.onExit();
    } else {
        music_.onExit();
    }
}

void ScreenManager::tick()
{
    if (current_ == ScreenId::Clock) {
        clock_.onTick();
    } else {
        music_.onTick();
    }
}

void ScreenManager::attachGestureHandler(lv_obj_t* root)
{
    if (!root) {
        return;
    }
    enableTouchEvents(root, false);

    if (root == gesture_root_) {
        return;
    }

    detachGestureHandler();
    lv_obj_add_event_cb(root, onGestureEvent, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(root, onGestureEvent, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(root, onGestureEvent, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(root, onGestureEvent, LV_EVENT_PRESS_LOST, this);
    gesture_root_ = root;
}

void ScreenManager::detachGestureHandler()
{
    if (!gesture_root_) {
        return;
    }
    while (lv_obj_remove_event_cb(gesture_root_, onGestureEvent)) {
    }
    gesture_root_ = nullptr;
}

void ScreenManager::onGestureEvent(lv_event_t* event)
{
    auto* manager = static_cast<ScreenManager*>(lv_event_get_user_data(event));
    if (!manager) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    const uint32_t tick = lv_tick_get();
    if (code == LV_EVENT_PRESSED) {
        manager->swipe_detector_.press(currentTouchPoint(), tick);
    } else if (code == LV_EVENT_PRESSING) {
        manager->swipe_detector_.move(currentTouchPoint());
    } else if (code == LV_EVENT_RELEASED) {
        SwipeGestureStats stats{};
        const SwipeDirection swipe =
            manager->swipe_detector_.release(currentTouchPoint(), tick, &stats);
        manager->handleSwipe(swipe, stats);
    } else if (code == LV_EVENT_PRESS_LOST) {
        manager->swipe_detector_.reset();
    }
}

void ScreenManager::onTickTimer(lv_timer_t* timer)
{
    auto* manager = static_cast<ScreenManager*>(timer->user_data);
    if (manager) {
        manager->tick();
    }
}

void ScreenManager::handleSwipe(SwipeDirection swipe, const SwipeGestureStats& stats)
{
    const ScreenId target = nextScreenForSwipe(current_, swipe);
    if (target != current_) {
        ESP_LOGI(kTag, "switch %s -> %s by swipe %s dx=%d dy=%d dt=%lu samples=%u",
                 screenName(current_), screenName(target), swipeName(swipe),
                 stats.dx, stats.dy, static_cast<unsigned long>(stats.duration_ms),
                 static_cast<unsigned>(stats.samples));
        switchTo(target);
    }
}

void ScreenManager::switchTo(ScreenId target)
{
    if (target == current_) {
        return;
    }

    if (current_ == ScreenId::Clock) {
        clock_.onExit();
    } else {
        music_.onExit();
    }

    if (target == ScreenId::Clock) {
        clock_.onEnter();
    } else {
        music_.onEnter();
    }

    current_ = target;
    attachGestureHandler(lv_scr_act());
}
