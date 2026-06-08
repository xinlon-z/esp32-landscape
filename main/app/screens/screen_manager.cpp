#include "app/screens/screen_manager.h"

#include <stdint.h>

#include "app/screens/gesture_feedback_model.h"
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

SwipeDirection swipeFromLvglDir(lv_dir_t dir)
{
    if (dir == LV_DIR_LEFT) {
        return SwipeDirection::Left;
    }
    if (dir == LV_DIR_RIGHT) {
        return SwipeDirection::Right;
    }
    return SwipeDirection::None;
}

void clearObjStyle(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
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
    clearGestureOverlayPointers();
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

    clearGestureFeedback();
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
    lv_obj_add_event_cb(root, onGestureEvent, LV_EVENT_GESTURE, this);
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
        manager->clearGestureFeedback();
        manager->swipe_detector_.press(currentTouchPoint(), tick);
    } else if (code == LV_EVENT_PRESSING) {
        manager->swipe_detector_.move(currentTouchPoint());
        SwipeGestureProgress progress{};
        if (manager->swipe_detector_.progress(&progress)) {
            manager->updateGestureFeedback(progress);
        }
    } else if (code == LV_EVENT_GESTURE) {
        lv_indev_t* indev = lv_indev_get_act();
        const SwipeDirection lvgl_swipe =
            indev ? swipeFromLvglDir(lv_indev_get_gesture_dir(indev)) : SwipeDirection::None;
        if (lvgl_swipe == SwipeDirection::None) {
            return;
        }

        const TouchPoint point = currentTouchPoint();
        manager->swipe_detector_.move(point);

        SwipeGestureStats stats{};
        const SwipeDirection swipe = manager->swipe_detector_.classify(point, tick, &stats);
        if (swipe == lvgl_swipe) {
            manager->swipe_detector_.reset();
            manager->handleSwipe(swipe, stats);
            lv_indev_wait_release(indev);
        }
    } else if (code == LV_EVENT_RELEASED) {
        SwipeGestureStats stats{};
        const SwipeDirection swipe =
            manager->swipe_detector_.release(currentTouchPoint(), tick, &stats);
        if (swipe == SwipeDirection::None) {
            manager->clearGestureFeedback();
        }
        manager->handleSwipe(swipe, stats);
    } else if (code == LV_EVENT_PRESS_LOST) {
        manager->swipe_detector_.reset();
        manager->clearGestureFeedback();
    }
}

void ScreenManager::onTickTimer(lv_timer_t* timer)
{
    auto* manager = static_cast<ScreenManager*>(timer->user_data);
    if (manager) {
        manager->tick();
    }
}

void ScreenManager::ensureGestureOverlay()
{
    lv_obj_t* root = lv_scr_act();
    if (!root) {
        return;
    }
    if (gesture_overlay_ && gesture_screen_root_ == root) {
        lv_obj_move_foreground(gesture_overlay_);
        return;
    }

    gesture_screen_root_ = root;
    gesture_overlay_ = lv_obj_create(root);
    lv_obj_set_size(gesture_overlay_, kGestureCueScreenW, kGestureCueScreenH);
    lv_obj_set_pos(gesture_overlay_, 0, 0);
    clearObjStyle(gesture_overlay_);
    lv_obj_clear_flag(gesture_overlay_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(gesture_overlay_, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(gesture_overlay_, LV_OBJ_FLAG_HIDDEN);

    gesture_pill_ = lv_obj_create(gesture_overlay_);
    lv_obj_set_size(gesture_pill_, kGestureCueHandleW, kGestureCueHandleH);
    lv_obj_set_pos(gesture_pill_, kGestureCueHandleX,
                   (kGestureCueScreenH - kGestureCueHandleH) / 2);
    clearObjStyle(gesture_pill_);
    lv_obj_clear_flag(gesture_pill_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(gesture_pill_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(gesture_pill_, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(gesture_pill_, LV_OPA_TRANSP, 0);

    gesture_arrow_slot_ = lv_obj_create(gesture_overlay_);
    lv_obj_set_size(gesture_arrow_slot_, kGestureCueArrowSlotW, kGestureCueArrowSlotH);
    lv_obj_set_pos(gesture_arrow_slot_, kGestureCueArrowSlotX,
                   (kGestureCueScreenH - kGestureCueArrowSlotH) / 2);
    clearObjStyle(gesture_arrow_slot_);
    lv_obj_clear_flag(gesture_arrow_slot_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(gesture_arrow_slot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(gesture_arrow_slot_, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(gesture_arrow_slot_, LV_OPA_TRANSP, 0);

    gesture_arrow_ = lv_label_create(gesture_arrow_slot_);
    clearObjStyle(gesture_arrow_);
    lv_obj_clear_flag(gesture_arrow_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_color(gesture_arrow_, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_opa(gesture_arrow_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_align(gesture_arrow_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(gesture_arrow_, LV_SYMBOL_RIGHT);
    lv_obj_center(gesture_arrow_);

    lv_obj_move_foreground(gesture_overlay_);
}

void ScreenManager::updateGestureFeedback(const SwipeGestureProgress& progress)
{
    if (progress.direction == SwipeDirection::None || progress.per_mille == 0) {
        clearGestureFeedback();
        return;
    }

    const GestureCueLayout layout = buildGestureCueLayout(progress);
    if (!layout.visible) {
        clearGestureFeedback();
        return;
    }

    ensureGestureOverlay();
    if (!gesture_overlay_ || !gesture_pill_ || !gesture_arrow_slot_ || !gesture_arrow_ ||
        !gesture_screen_root_) {
        return;
    }

    lv_obj_clear_flag(gesture_overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(gesture_overlay_);

    lv_obj_set_pos(gesture_pill_, layout.handle_x, layout.handle_y);
    lv_obj_set_size(gesture_pill_, layout.handle_w, layout.handle_h);
    lv_obj_set_style_bg_opa(gesture_pill_, static_cast<lv_opa_t>(layout.handle_opa), 0);

    lv_obj_set_pos(gesture_arrow_slot_, layout.arrow_slot_x, layout.arrow_slot_y);
    lv_obj_set_size(gesture_arrow_slot_, layout.arrow_slot_w, layout.arrow_slot_h);

    lv_label_set_text(gesture_arrow_, layout.swipe_right ? LV_SYMBOL_RIGHT : LV_SYMBOL_LEFT);
    lv_obj_center(gesture_arrow_);
    lv_obj_set_style_text_opa(gesture_arrow_, static_cast<lv_opa_t>(layout.arrow_opa), 0);
}

void ScreenManager::clearGestureFeedback()
{
    if (gesture_pill_) {
        lv_obj_set_style_bg_opa(gesture_pill_, LV_OPA_TRANSP, 0);
    }
    if (gesture_arrow_) {
        lv_obj_set_style_text_opa(gesture_arrow_, LV_OPA_TRANSP, 0);
    }
    if (gesture_overlay_) {
        lv_obj_add_flag(gesture_overlay_, LV_OBJ_FLAG_HIDDEN);
    }
}

void ScreenManager::clearGestureOverlayPointers()
{
    gesture_overlay_ = nullptr;
    gesture_pill_ = nullptr;
    gesture_arrow_slot_ = nullptr;
    gesture_arrow_ = nullptr;
    gesture_screen_root_ = nullptr;
}

void ScreenManager::handleSwipe(SwipeDirection swipe, const SwipeGestureStats& stats)
{
    const ScreenId target = nextScreenForSwipe(current_, swipe);
    if (target == current_) {
        clearGestureFeedback();
        return;
    }

    ESP_LOGI(kTag, "switch %s -> %s by swipe %s dx=%d dy=%d dt=%lu samples=%u edge=%d",
             screenName(current_), screenName(target), swipeName(swipe),
             stats.dx, stats.dy, static_cast<unsigned long>(stats.duration_ms),
             static_cast<unsigned>(stats.samples), stats.edge_start ? 1 : 0);

    clearGestureFeedback();
    switchTo(target);
}

void ScreenManager::switchTo(ScreenId target)
{
    if (target == current_) {
        return;
    }

    clearGestureFeedback();
    clearGestureOverlayPointers();

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
    clearGestureOverlayPointers();
    attachGestureHandler(lv_scr_act());
}
