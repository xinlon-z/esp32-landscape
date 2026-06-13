#include <gtest/gtest.h>

#include "platform/button_state.h"

TEST(ButtonState, ShortPressAfterDebouncedRelease)
{
    DebouncedButton button(2, 5);

    EXPECT_EQ(button.update(true), ButtonEvent::None);
    EXPECT_EQ(button.update(true), ButtonEvent::None);
    EXPECT_EQ(button.update(true), ButtonEvent::None);
    EXPECT_EQ(button.update(false), ButtonEvent::None);
    EXPECT_EQ(button.update(false), ButtonEvent::ShortPress);
    EXPECT_EQ(button.update(false), ButtonEvent::None);
}

TEST(ButtonState, IgnoresSingleTickBounce)
{
    DebouncedButton button(2, 5);

    EXPECT_EQ(button.update(true), ButtonEvent::None);
    EXPECT_EQ(button.update(false), ButtonEvent::None);
    EXPECT_EQ(button.update(false), ButtonEvent::None);
    EXPECT_EQ(button.update(true), ButtonEvent::None);
    EXPECT_EQ(button.update(true), ButtonEvent::None);
    EXPECT_EQ(button.update(false), ButtonEvent::None);
    EXPECT_EQ(button.update(false), ButtonEvent::ShortPress);
}

TEST(ButtonState, LongPressFiresOnce)
{
    DebouncedButton button(2, 3);

    EXPECT_EQ(button.update(true), ButtonEvent::None);
    EXPECT_EQ(button.update(true), ButtonEvent::None);
    EXPECT_EQ(button.update(true), ButtonEvent::None);
    EXPECT_EQ(button.update(true), ButtonEvent::LongPress);
    EXPECT_EQ(button.update(true), ButtonEvent::None);
    EXPECT_EQ(button.update(true), ButtonEvent::None);
    EXPECT_EQ(button.update(false), ButtonEvent::None);
    EXPECT_EQ(button.update(false), ButtonEvent::None);
}

TEST(ButtonState, ResetClearsPressState)
{
    DebouncedButton button(2, 3);

    EXPECT_EQ(button.update(true), ButtonEvent::None);
    EXPECT_EQ(button.update(true), ButtonEvent::None);
    button.reset();
    EXPECT_EQ(button.update(false), ButtonEvent::None);
    EXPECT_EQ(button.update(false), ButtonEvent::None);
}
