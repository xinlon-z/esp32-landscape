#include "../main/app/screens/gesture_manager.cpp"

#include <stdio.h>

static int expectSwipe(const char* name, TouchPoint start, TouchPoint end, SwipeDirection expected)
{
    const SwipeDirection actual = detectSwipe(start, end);
    if (actual != expected) {
        printf("%s expected %d got %d\n",
               name, static_cast<int>(expected), static_cast<int>(actual));
        return 1;
    }
    return 0;
}

static int expectNext(const char* name, ScreenId current, SwipeDirection direction, ScreenId expected)
{
    const ScreenId actual = nextScreenForSwipe(current, direction);
    if (actual != expected) {
        printf("%s expected %d got %d\n",
               name, static_cast<int>(expected), static_cast<int>(actual));
        return 1;
    }
    return 0;
}

static int expectFeatureActionEvent()
{
    constexpr uint8_t kActionId = 7;

    EventBus::get().resetForTest();
    publishFeatureAction(ScreenId::Music, kActionId);

    AppEvent event{};
    if (!EventBus::get().poll(&event)) {
        printf("feature action event missing\n");
        return 1;
    }
    if (event.type != AppEventType::FeatureAction) {
        printf("feature action expected type %d got %d\n",
               static_cast<int>(AppEventType::FeatureAction),
               static_cast<int>(event.type));
        return 1;
    }
    if (event.payload.feature_action.screen_id != static_cast<uint8_t>(ScreenId::Music)) {
        printf("feature action expected screen %d got %d\n",
               static_cast<int>(ScreenId::Music),
               static_cast<int>(event.payload.feature_action.screen_id));
        return 1;
    }
    if (event.payload.feature_action.action_id != kActionId) {
        printf("feature action expected action %d got %d\n",
               static_cast<int>(kActionId),
               static_cast<int>(event.payload.feature_action.action_id));
        return 1;
    }
    if (EventBus::get().poll(&event)) {
        printf("feature action queue not empty\n");
        return 1;
    }

    return 0;
}

int main()
{
    int failures = 0;
    failures += expectSwipe("left swipe", {200, 100}, {40, 110}, SwipeDirection::Left);
    failures += expectSwipe("right swipe", {40, 100}, {200, 110}, SwipeDirection::Right);
    failures += expectSwipe("short drag", {40, 100}, {120, 110}, SwipeDirection::None);
    failures += expectSwipe("vertical drag", {40, 100}, {200, 180}, SwipeDirection::None);

    failures += expectNext("clock left", ScreenId::Clock, SwipeDirection::Left, ScreenId::Music);
    failures += expectNext("music right", ScreenId::Music, SwipeDirection::Right, ScreenId::Clock);
    failures += expectNext("clock right ignored", ScreenId::Clock, SwipeDirection::Right, ScreenId::Clock);
    failures += expectNext("music left ignored", ScreenId::Music, SwipeDirection::Left, ScreenId::Music);
    failures += expectNext("clock none ignored", ScreenId::Clock, SwipeDirection::None, ScreenId::Clock);
    failures += expectNext("music none ignored", ScreenId::Music, SwipeDirection::None, ScreenId::Music);

    SwipeGestureDetector detector;
    detector.press({200, 100});
    if (detector.release({40, 110}) != SwipeDirection::Left) {
        printf("detector left swipe failed\n");
        failures++;
    }
    if (detector.release({40, 110}) != SwipeDirection::None) {
        printf("detector released state failed\n");
        failures++;
    }
    detector.press({40, 100});
    detector.reset();
    if (detector.release({200, 110}) != SwipeDirection::None) {
        printf("detector reset failed\n");
        failures++;
    }
    failures += expectFeatureActionEvent();

    return failures == 0 ? 0 : 1;
}
