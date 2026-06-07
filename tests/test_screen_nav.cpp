#include <gtest/gtest.h>
#include "app/screens/gesture_manager.cpp"
#include "app/core/event/event_bus.cpp"

TEST(ScreenNav, SwipeAndTransition)
{
    EXPECT_EQ(detectSwipe({200, 100}, {40, 110}), SwipeDirection::Left) << "left swipe";
    EXPECT_EQ(detectSwipe({40, 100}, {200, 110}), SwipeDirection::Right) << "right swipe";
    EXPECT_EQ(detectSwipe({40, 100}, {120, 110}), SwipeDirection::None) << "short drag";
    EXPECT_EQ(detectSwipe({40, 100}, {200, 180}), SwipeDirection::None) << "vertical drag";

    EXPECT_EQ(nextScreenForSwipe(ScreenId::Clock, SwipeDirection::Left), ScreenId::Music) << "clock left";
    EXPECT_EQ(nextScreenForSwipe(ScreenId::Music, SwipeDirection::Right), ScreenId::Clock) << "music right";
    EXPECT_EQ(nextScreenForSwipe(ScreenId::Clock, SwipeDirection::Right), ScreenId::Clock) << "clock right ignored";
    EXPECT_EQ(nextScreenForSwipe(ScreenId::Music, SwipeDirection::Left), ScreenId::Music) << "music left ignored";
    EXPECT_EQ(nextScreenForSwipe(ScreenId::Clock, SwipeDirection::None), ScreenId::Clock) << "clock none ignored";
    EXPECT_EQ(nextScreenForSwipe(ScreenId::Music, SwipeDirection::None), ScreenId::Music) << "music none ignored";

    SwipeGestureDetector detector;
    detector.press({200, 100});
    EXPECT_EQ(detector.release({40, 110}), SwipeDirection::Left) << "detector left swipe failed";
    EXPECT_EQ(detector.release({40, 110}), SwipeDirection::None) << "detector released state failed";
    detector.press({40, 100});
    detector.reset();
    EXPECT_EQ(detector.release({200, 110}), SwipeDirection::None) << "detector reset failed";

    // expectFeatureActionEvent inlined
    {
        constexpr uint8_t kActionId = 7;

        EventBus::get().resetForTest();
        publishFeatureAction(ScreenId::Music, kActionId);

        AppEvent event{};
        EXPECT_TRUE(EventBus::get().poll(&event)) << "feature action event missing";
        EXPECT_EQ(event.type, AppEventType::FeatureAction)
            << "feature action expected type " << static_cast<int>(AppEventType::FeatureAction)
            << " got " << static_cast<int>(event.type);
        EXPECT_EQ(event.payload.feature_action.screen_id, static_cast<uint8_t>(ScreenId::Music))
            << "feature action expected screen " << static_cast<int>(ScreenId::Music)
            << " got " << static_cast<int>(event.payload.feature_action.screen_id);
        EXPECT_EQ(event.payload.feature_action.action_id, kActionId)
            << "feature action expected action " << static_cast<int>(kActionId)
            << " got " << static_cast<int>(event.payload.feature_action.action_id);
        EXPECT_FALSE(EventBus::get().poll(&event)) << "feature action queue not empty";
    }
}
