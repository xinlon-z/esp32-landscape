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

TEST(ScreenNav, SwipeTimingAndEdgeRules)
{
    SwipeGestureDetector detector;
    SwipeGestureStats stats{};

    detector.press({40, 80}, 1000);
    detector.move({120, 81});
    EXPECT_EQ(detector.release({183, 81}, 1665, &stats), SwipeDirection::None)
        << "production ghost drift dx=143 dt=665 must not navigate";

    detector.press({620, 100}, 2000);
    detector.move({420, 104});
    EXPECT_EQ(detector.release({193, 121}, 2095, &stats), SwipeDirection::Left)
        << "fast right-edge left swipe should remain accepted";

    detector.press({250, 90}, 3000);
    EXPECT_EQ(detector.release({380, 92}, 3700, &stats), SwipeDirection::None)
        << "slow center drift should still be rejected";

    detector.press({250, 90}, 4000);
    EXPECT_EQ(detector.release({480, 92}, 4160, &stats), SwipeDirection::Right)
        << "fast deliberate center swipe should still work";
}

TEST(ScreenNav, DeliberateHumanSpeedSwipesAreAccepted)
{
    SwipeGestureDetector detector;
    SwipeGestureStats stats{};

    detector.press({28, 90}, 1000);
    detector.move({88, 91});
    EXPECT_EQ(detector.release({154, 92}, 1500, &stats), SwipeDirection::Right)
        << "edge swipe at normal finger speed should navigate";
    EXPECT_TRUE(stats.edge_start);

    detector.press({28, 90}, 3000);
    detector.move({62, 91});
    EXPECT_EQ(detector.release({96, 92}, 3500, &stats), SwipeDirection::Right)
        << "short edge swipe should be sensitive enough for a narrow screen";
    EXPECT_TRUE(stats.edge_start);

    detector.press({250, 90}, 2000);
    detector.move({318, 91});
    EXPECT_EQ(detector.release({390, 92}, 2440, &stats), SwipeDirection::Right)
        << "center swipe should not require a flick-speed gesture";
    EXPECT_FALSE(stats.edge_start);
}

TEST(ScreenNav, ClassifyDuringDragDoesNotResetDetector)
{
    SwipeGestureDetector detector;
    SwipeGestureStats stats{};

    detector.press({28, 90}, 1000);
    detector.move({62, 91});

    EXPECT_EQ(detector.classify({96, 92}, 1500, &stats), SwipeDirection::Right);
    EXPECT_TRUE(stats.edge_start);
    EXPECT_EQ(detector.release({110, 92}, 1540, &stats), SwipeDirection::Right)
        << "live classification must not consume the release fallback";
}

TEST(ScreenNav, DragProgressReportsDirectionAndClamps)
{
    SwipeGestureDetector detector;
    SwipeGestureProgress progress{};

    detector.press({620, 90}, 100);
    detector.move({560, 92});
    EXPECT_TRUE(detector.progress(&progress));
    EXPECT_EQ(progress.direction, SwipeDirection::Left);
    EXPECT_GT(progress.per_mille, 0u);
    EXPECT_LT(progress.per_mille, 1000u);

    detector.move({430, 92});
    EXPECT_TRUE(detector.progress(&progress));
    EXPECT_EQ(progress.per_mille, 1000u);

    detector.reset();
    EXPECT_FALSE(detector.progress(&progress));
}
