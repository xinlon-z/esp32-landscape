#include "app/core/event/event_queue.h"
#include "app/core/event/app_events.h"

#include <gtest/gtest.h>

TEST(EventQueue, PushPollOverflow)
{
    EventQueue<AppEvent, 2> queue;
    AppEvent first{};
    first.type = AppEventType::MusicStateChanged;
    first.payload.music_state.revision = 10;

    AppEvent second{};
    second.type = AppEventType::CoverStateChanged;
    second.payload.cover_state.cover_id = 7;
    second.payload.cover_state.status = CoverStatus::Ready;

    AppEvent third{};
    third.type = AppEventType::ClockTimeChanged;
    third.payload.clock_time.revision = 3;

    EXPECT_TRUE(queue.poll(nullptr) == false) << "poll nullptr should return false";
    EXPECT_TRUE(queue.poll(&third) == false) << "empty queue should return false";
    EXPECT_TRUE(queue.publish(first)) << "first publish should succeed";
    EXPECT_TRUE(queue.publish(second)) << "second publish should succeed";
    EXPECT_TRUE(!queue.publish(third)) << "overflow publish should fail";
    EXPECT_TRUE(queue.overflowCount() == 1) << "overflow count should be 1";

    AppEvent out{};
    EXPECT_TRUE(queue.poll(&out)) << "first poll should succeed";
    EXPECT_TRUE(out.type == AppEventType::MusicStateChanged) << "first event type mismatch";
    EXPECT_TRUE(out.payload.music_state.revision == 10) << "first revision mismatch";
    EXPECT_TRUE(queue.poll(&out)) << "second poll should succeed";
    EXPECT_TRUE(out.type == AppEventType::CoverStateChanged) << "second event type mismatch";
    EXPECT_TRUE(out.payload.cover_state.cover_id == 7) << "cover id mismatch";
    EXPECT_TRUE(out.payload.cover_state.status == CoverStatus::Ready) << "cover status mismatch";
    EXPECT_TRUE(!queue.poll(&out)) << "queue should be empty";

    EventQueue<AppEvent, 0> zero;
    EXPECT_TRUE(!zero.publish(first)) << "zero-capacity publish should fail";
    EXPECT_TRUE(!zero.poll(&out)) << "zero-capacity poll should fail";
    EXPECT_TRUE(zero.overflowCount() == 1) << "zero-capacity overflow count should be 1";
}
