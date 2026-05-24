#include "../main/app/core/event/event_queue.h"
#include "../main/app/core/event/app_events.h"

#include <stdio.h>

static int expect(bool condition, const char* message)
{
    if (!condition) {
        printf("%s\n", message);
        return 1;
    }
    return 0;
}

int main()
{
    int failures = 0;

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

    failures += expect(queue.poll(nullptr) == false, "poll nullptr should return false");
    failures += expect(queue.poll(&third) == false, "empty queue should return false");
    failures += expect(queue.publish(first), "first publish should succeed");
    failures += expect(queue.publish(second), "second publish should succeed");
    failures += expect(!queue.publish(third), "overflow publish should fail");
    failures += expect(queue.overflowCount() == 1, "overflow count should be 1");

    AppEvent out{};
    failures += expect(queue.poll(&out), "first poll should succeed");
    failures += expect(out.type == AppEventType::MusicStateChanged, "first event type mismatch");
    failures += expect(out.payload.music_state.revision == 10, "first revision mismatch");
    failures += expect(queue.poll(&out), "second poll should succeed");
    failures += expect(out.type == AppEventType::CoverStateChanged, "second event type mismatch");
    failures += expect(out.payload.cover_state.cover_id == 7, "cover id mismatch");
    failures += expect(out.payload.cover_state.status == CoverStatus::Ready, "cover status mismatch");
    failures += expect(!queue.poll(&out), "queue should be empty");

    return failures == 0 ? 0 : 1;
}
