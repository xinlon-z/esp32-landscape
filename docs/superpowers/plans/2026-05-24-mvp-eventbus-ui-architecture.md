# MVP EventBus UI Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the existing LVGL clock and music UI into the approved MVP + EventBus architecture without changing visible behavior.

**Architecture:** Add a `main/app` tree with a lightweight UI EventBus, service snapshots, screen lifecycle wrappers, and feature-level MVP boundaries. Services publish lightweight events and own snapshots; active screen presenters consume events only from `Screen::onTick()` while views remain LVGL-only renderers.

**Tech Stack:** ESP-IDF C++17, LVGL, FreeRTOS, existing standalone C++ tests, existing SDL music UI simulator, `cmake --build build` for firmware verification.

---

## File Structure

- Create `main/app/core/event/app_events.h`: event enums, payload structs, `AppEvent`, trivially-copyable assertions.
- Create `main/app/core/event/event_queue.h`: fixed-size ring buffer used by EventBus and unit tests.
- Create `main/app/core/event/event_bus.h/cpp`: singleton UI EventBus with non-blocking `publish()` and `poll()`.
- Create `main/app/screens/screen.h`: pure screen lifecycle interface.
- Create `main/app/screens/gesture_manager.h/cpp`: move swipe detection and feature action publishing.
- Create `main/app/screens/screen_manager.h/cpp`: screen ownership, navigation, and active-screen tick routing.
- Create `main/app/screens/clock_screen.h/cpp`: owns `ClockView` and `ClockPresenter`.
- Create `main/app/screens/music_screen.h/cpp`: owns `MusicView` and `MusicPresenter`.
- Create `main/app/services/time_service.h/cpp`: RTC snapshot service.
- Create `main/app/services/power_service.h/cpp`: wrapper around `PowerManager::getState()`.
- Create `main/app/services/network_service.h/cpp`: wrapper around `ClockNet::getStatus()`.
- Create `main/app/services/mqtt_service.h/cpp`: wrapper around current `MusicMqtt` state and Shairport parsing.
- Create `main/app/services/cover_service.h/cpp`: owner for cover lifecycle and active cover buffers.
- Create `main/app/features/clock/clock_model.h/cpp`: UI-thread clock display model.
- Create `main/app/features/clock/clock_presenter.h/cpp`: clock snapshot/event presenter.
- Create `main/app/features/clock/clock_view.h/cpp`: moved LVGL clock view.
- Create `main/app/features/clock/widgets/seven_segment_widget.h/cpp`: seven-segment LVGL widget.
- Create `main/app/features/music/music_state.h`: header-only shared music data.
- Create `main/app/features/music/music_model.h/cpp`: UI-thread music display model.
- Create `main/app/features/music/music_presenter.h/cpp`: music snapshot/event presenter.
- Create `main/app/features/music/music_view.h/cpp`: moved LVGL music view.
- Create `main/app/features/music/widgets/cover_widget.h/cpp`: cover LVGL widget wrapper.
- Create `main/app/features/music/widgets/visualizer_widget.h/cpp`: visualizer LVGL widget wrapper.
- Create `main/app/ui/fonts/music_fonts.h/cpp`: TinyTTF font creation currently in `music_player_screen.cpp`.
- Create `main/app/ui/styles/lvgl_style_helpers.h/cpp`: shared LVGL style helpers.
- Modify `main/main.cpp`: initialize services and `ScreenManager`.
- Modify `main/CMakeLists.txt`: include new sources and remove old screen sources after migration.
- Modify `sim/main.cpp` and `sim/CMakeLists.txt`: instantiate the new music screen or feature harness.
- Modify tests under `tests/`: add EventQueue, services, models, and navigation tests.

---

### Task 1: Event Types And Queue

**Files:**
- Create: `main/app/core/event/app_events.h`
- Create: `main/app/core/event/event_queue.h`
- Create: `tests/test_event_queue.cpp`

- [ ] **Step 1: Write the failing EventQueue test**

Create `tests/test_event_queue.cpp`:

```cpp
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
    failures += expect(!queue.poll(&out), "queue should be empty");

    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```bash
c++ -std=c++17 -I. tests/test_event_queue.cpp -o /tmp/test_event_queue && /tmp/test_event_queue
```

Expected: compile failure because `main/app/core/event/event_queue.h` does not exist.

- [ ] **Step 3: Add event payload definitions**

Create `main/app/core/event/app_events.h`:

```cpp
#pragma once

#include <stdint.h>
#include <type_traits>

enum class ScreenId : uint8_t {
    Clock = 0,
    Music = 1,
};

enum class AppEventType : uint8_t {
    ClockTimeChanged,
    PowerStateChanged,
    NetworkStateChanged,
    MusicStateChanged,
    CoverStateChanged,
    FeatureAction,
};

enum class CoverStatus : uint8_t {
    Idle,
    Loading,
    Ready,
    Error,
};

struct ClockTimeChangedEvent {
    uint32_t revision = 0;
};

struct PowerStateChangedEvent {
    uint32_t revision = 0;
};

struct NetworkStateChangedEvent {
    uint32_t revision = 0;
};

struct MusicStateChangedEvent {
    uint32_t revision = 0;
};

struct CoverStateChangedEvent {
    uint32_t cover_id = 0;
    CoverStatus status = CoverStatus::Idle;
};

struct FeatureActionEvent {
    uint8_t screen_id = 0;
    uint8_t action_id = 0;
};

struct AppEvent {
    AppEventType type = AppEventType::ClockTimeChanged;
    union Payload {
        ClockTimeChangedEvent clock_time;
        PowerStateChangedEvent power_state;
        NetworkStateChangedEvent network_state;
        MusicStateChangedEvent music_state;
        CoverStateChangedEvent cover_state;
        FeatureActionEvent feature_action;

        Payload() : clock_time{} {}
    } payload;
};

static_assert(std::is_trivially_copyable_v<ClockTimeChangedEvent>);
static_assert(std::is_trivially_copyable_v<PowerStateChangedEvent>);
static_assert(std::is_trivially_copyable_v<NetworkStateChangedEvent>);
static_assert(std::is_trivially_copyable_v<MusicStateChangedEvent>);
static_assert(std::is_trivially_copyable_v<CoverStateChangedEvent>);
static_assert(std::is_trivially_copyable_v<FeatureActionEvent>);
static_assert(std::is_trivially_copyable_v<AppEvent>);
```

- [ ] **Step 4: Add fixed-size EventQueue**

Create `main/app/core/event/event_queue.h`:

```cpp
#pragma once

#include <stddef.h>
#include <stdint.h>

template <typename T, size_t Capacity>
class EventQueue {
public:
    bool publish(const T& event)
    {
        if (Capacity == 0 || count_ == Capacity) {
            ++overflow_count_;
            return false;
        }
        items_[tail_] = event;
        tail_ = (tail_ + 1) % Capacity;
        ++count_;
        return true;
    }

    bool poll(T* event)
    {
        if (!event || count_ == 0) {
            return false;
        }
        *event = items_[head_];
        head_ = (head_ + 1) % Capacity;
        --count_;
        return true;
    }

    size_t size() const { return count_; }
    uint32_t overflowCount() const { return overflow_count_; }

private:
    T items_[Capacity == 0 ? 1 : Capacity]{};
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;
    uint32_t overflow_count_ = 0;
};
```

- [ ] **Step 5: Run the EventQueue test**

Run:

```bash
c++ -std=c++17 -I. tests/test_event_queue.cpp -o /tmp/test_event_queue && /tmp/test_event_queue
```

Expected: command exits `0`.

- [ ] **Step 6: Commit Task 1**

Run:

```bash
git add main/app/core/event/app_events.h main/app/core/event/event_queue.h tests/test_event_queue.cpp
git commit -m "Add UI event queue primitives"
```

---

### Task 2: EventBus Singleton

**Files:**
- Create: `main/app/core/event/event_bus.h`
- Create: `main/app/core/event/event_bus.cpp`
- Create: `tests/test_event_bus.cpp`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Write the failing EventBus test**

Create `tests/test_event_bus.cpp`:

```cpp
#include "../main/app/core/event/event_bus.cpp"

#include <stdio.h>

int main()
{
    EventBus::get().resetForTest();

    AppEvent event{};
    event.type = AppEventType::PowerStateChanged;
    event.payload.power_state.revision = 42;

    if (!EventBus::get().publish(event)) {
        printf("publish failed\n");
        return 1;
    }

    AppEvent out{};
    if (!EventBus::get().poll(&out)) {
        printf("poll failed\n");
        return 1;
    }
    if (out.type != AppEventType::PowerStateChanged || out.payload.power_state.revision != 42) {
        printf("event mismatch\n");
        return 1;
    }
    if (EventBus::get().poll(&out)) {
        printf("poll should return false on empty queue\n");
        return 1;
    }
    return 0;
}
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```bash
c++ -std=c++17 -I. tests/test_event_bus.cpp -o /tmp/test_event_bus && /tmp/test_event_bus
```

Expected: compile failure because `event_bus.cpp` does not exist.

- [ ] **Step 3: Add EventBus interface**

Create `main/app/core/event/event_bus.h`:

```cpp
#pragma once

#include "app_events.h"
#include "event_queue.h"

#include <mutex>
#include <stdint.h>

class EventBus {
public:
    static EventBus& get();

    bool publish(const AppEvent& event);
    bool poll(AppEvent* event);
    uint32_t overflowCount() const;

    void resetForTest();

private:
    EventBus() = default;

    static constexpr size_t kCapacity = 16;
    mutable std::mutex mutex_;
    EventQueue<AppEvent, kCapacity> queue_;
};
```

- [ ] **Step 4: Add EventBus implementation**

Create `main/app/core/event/event_bus.cpp`:

```cpp
#include "event_bus.h"

EventBus& EventBus::get()
{
    static EventBus bus;
    return bus;
}

bool EventBus::publish(const AppEvent& event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.publish(event);
}

bool EventBus::poll(AppEvent* event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.poll(event);
}

uint32_t EventBus::overflowCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.overflowCount();
}

void EventBus::resetForTest()
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_ = EventQueue<AppEvent, kCapacity>{};
}
```

- [ ] **Step 5: Run the EventBus test**

Run:

```bash
c++ -std=c++17 -I. tests/test_event_bus.cpp -o /tmp/test_event_bus && /tmp/test_event_bus
```

Expected: command exits `0`.

- [ ] **Step 6: Add EventBus source to firmware build**

Modify `main/CMakeLists.txt` and add:

```cmake
       "app/core/event/event_bus.cpp"
```

under the `SRCS` list.

- [ ] **Step 7: Commit Task 2**

Run:

```bash
git add main/app/core/event/event_bus.h main/app/core/event/event_bus.cpp tests/test_event_bus.cpp main/CMakeLists.txt
git commit -m "Add UI event bus singleton"
```

---

### Task 3: Screen And Gesture Layer

**Files:**
- Create: `main/app/screens/screen.h`
- Create: `main/app/screens/gesture_manager.h`
- Create: `main/app/screens/gesture_manager.cpp`
- Modify: `tests/test_screen_nav.cpp`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Move the navigation test to the new API**

Modify `tests/test_screen_nav.cpp` to include the new file:

```cpp
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

    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Run test and verify it fails**

Run:

```bash
c++ -std=c++17 -I. tests/test_screen_nav.cpp -o /tmp/test_screen_nav && /tmp/test_screen_nav
```

Expected: compile failure because `main/app/screens/gesture_manager.cpp` does not exist.

- [ ] **Step 3: Add pure Screen interface**

Create `main/app/screens/screen.h`:

```cpp
#pragma once

class Screen {
public:
    virtual ~Screen() = default;
    virtual void onEnter() = 0;
    virtual void onExit() = 0;
    virtual void onTick() = 0;
};
```

- [ ] **Step 4: Add gesture manager API**

Create `main/app/screens/gesture_manager.h`:

```cpp
#pragma once

#include "app/core/event/app_events.h"

#include <stdint.h>

enum class SwipeDirection : uint8_t {
    None,
    Left,
    Right,
};

struct TouchPoint {
    int16_t x;
    int16_t y;
};

class SwipeGestureDetector {
public:
    void press(TouchPoint point);
    SwipeDirection release(TouchPoint point);
    void reset();

private:
    bool pressed_ = false;
    TouchPoint start_{0, 0};
};

SwipeDirection detectSwipe(TouchPoint start, TouchPoint end);
ScreenId nextScreenForSwipe(ScreenId current, SwipeDirection direction);
void publishFeatureAction(ScreenId screen, uint8_t action_id);
```

- [ ] **Step 5: Add gesture manager implementation**

Create `main/app/screens/gesture_manager.cpp`:

```cpp
#include "gesture_manager.h"

#include "app/core/event/event_bus.h"

namespace {
constexpr int kMinSwipeX = 120;
constexpr int kMaxSwipeY = 54;

int absInt(int v)
{
    return v < 0 ? -v : v;
}
} // namespace

void SwipeGestureDetector::press(TouchPoint point)
{
    pressed_ = true;
    start_ = point;
}

SwipeDirection SwipeGestureDetector::release(TouchPoint point)
{
    if (!pressed_) {
        return SwipeDirection::None;
    }
    pressed_ = false;
    return detectSwipe(start_, point);
}

void SwipeGestureDetector::reset()
{
    pressed_ = false;
    start_ = {0, 0};
}

SwipeDirection detectSwipe(TouchPoint start, TouchPoint end)
{
    const int dx = end.x - start.x;
    const int dy = end.y - start.y;
    if (absInt(dx) < kMinSwipeX || absInt(dy) > kMaxSwipeY) {
        return SwipeDirection::None;
    }
    return dx < 0 ? SwipeDirection::Left : SwipeDirection::Right;
}

ScreenId nextScreenForSwipe(ScreenId current, SwipeDirection direction)
{
    if (current == ScreenId::Clock && direction == SwipeDirection::Left) {
        return ScreenId::Music;
    }
    if (current == ScreenId::Music && direction == SwipeDirection::Right) {
        return ScreenId::Clock;
    }
    return current;
}

void publishFeatureAction(ScreenId screen, uint8_t action_id)
{
    AppEvent event{};
    event.type = AppEventType::FeatureAction;
    event.payload.feature_action.screen_id = static_cast<uint8_t>(screen);
    event.payload.feature_action.action_id = action_id;
    EventBus::get().publish(event);
}
```

- [ ] **Step 6: Run navigation test**

Run:

```bash
c++ -std=c++17 -I. tests/test_screen_nav.cpp main/app/core/event/event_bus.cpp -o /tmp/test_screen_nav && /tmp/test_screen_nav
```

Expected: command exits `0`.

- [ ] **Step 7: Add gesture source to firmware build**

Modify `main/CMakeLists.txt`:

```cmake
       "app/screens/gesture_manager.cpp"
```

- [ ] **Step 8: Commit Task 3**

Run:

```bash
git add main/app/screens/screen.h main/app/screens/gesture_manager.h main/app/screens/gesture_manager.cpp tests/test_screen_nav.cpp main/CMakeLists.txt
git commit -m "Move screen gestures into app layer"
```

---

### Task 4: Service Snapshot Wrappers

**Files:**
- Create: `main/app/services/time_service.h/cpp`
- Create: `main/app/services/power_service.h/cpp`
- Create: `main/app/services/network_service.h/cpp`
- Create: `tests/test_power_service.cpp`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Write a pure PowerService packing test**

Create `tests/test_power_service.cpp`:

```cpp
#include "../main/app/services/power_service.cpp"

#include <stdio.h>

int main()
{
    PowerSnapshot snapshot{};
    snapshot.external_power = true;
    snapshot.battery_percent = 87;
    snapshot.dimmed = true;
    snapshot.sleeping = false;
    snapshot.revision = 5;

    if (!snapshot.external_power || snapshot.battery_percent != 87 || !snapshot.dimmed || snapshot.sleeping) {
        printf("snapshot fields failed\n");
        return 1;
    }
    if (snapshot.revision != 5) {
        printf("revision failed\n");
        return 1;
    }
    return 0;
}
```

- [ ] **Step 2: Run test and verify it fails**

Run:

```bash
c++ -std=c++17 -I. -Itests/stubs tests/test_power_service.cpp main/app/core/event/event_bus.cpp -o /tmp/test_power_service && /tmp/test_power_service
```

Expected: compile failure because `PowerSnapshot` does not exist.

- [ ] **Step 3: Add TimeService API**

Create `main/app/services/time_service.h`:

```cpp
#pragma once

#include <stdint.h>

struct ClockSnapshot {
    bool rtc_ok = false;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t week = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint32_t revision = 0;
};

class TimeService {
public:
    static TimeService& get();
    ClockSnapshot snapshot();
    void poll();

private:
    TimeService() = default;
    ClockSnapshot snapshot_{};
};
```

- [ ] **Step 4: Add TimeService implementation**

Create `main/app/services/time_service.cpp`:

```cpp
#include "time_service.h"

#include "app/core/event/event_bus.h"
#include "i2c_equipment.h"

TimeService& TimeService::get()
{
    static TimeService service;
    return service;
}

ClockSnapshot TimeService::snapshot()
{
    return snapshot_;
}

void TimeService::poll()
{
    const RtcDateTime_t now = i2c_rtc_get();
    ClockSnapshot next{};
    next.rtc_ok = !(now.hour > 23 || now.minute > 59 ||
                    now.month == 0 || now.month > 12 ||
                    now.day == 0 || now.day > 31);
    next.hour = now.hour;
    next.minute = now.minute;
    next.second = now.second;
    next.week = now.week;
    next.month = now.month;
    next.day = now.day;
    next.revision = snapshot_.revision + 1;
    snapshot_ = next;

    AppEvent event{};
    event.type = AppEventType::ClockTimeChanged;
    event.payload.clock_time.revision = snapshot_.revision;
    EventBus::get().publish(event);
}
```

- [ ] **Step 5: Add PowerService API and implementation**

Create `main/app/services/power_service.h`:

```cpp
#pragma once

#include <stdint.h>

struct PowerSnapshot {
    bool external_power = false;
    int battery_percent = -1;
    bool dimmed = false;
    bool sleeping = false;
    uint32_t revision = 0;
};

class PowerService {
public:
    static PowerService& get();
    PowerSnapshot snapshot();
    void poll();

private:
    PowerService() = default;
    PowerSnapshot snapshot_{};
};
```

Create `main/app/services/power_service.cpp`:

```cpp
#include "power_service.h"

#include "app/core/event/event_bus.h"
#include "power_mgr.h"

PowerService& PowerService::get()
{
    static PowerService service;
    return service;
}

PowerSnapshot PowerService::snapshot()
{
    return snapshot_;
}

void PowerService::poll()
{
    const PowerManager::State state = PowerManager::getState();
    if (state.external_power == snapshot_.external_power &&
        state.battery_percent == snapshot_.battery_percent &&
        state.dimmed == snapshot_.dimmed &&
        state.sleeping == snapshot_.sleeping) {
        return;
    }

    snapshot_.external_power = state.external_power;
    snapshot_.battery_percent = state.battery_percent;
    snapshot_.dimmed = state.dimmed;
    snapshot_.sleeping = state.sleeping;
    ++snapshot_.revision;

    AppEvent event{};
    event.type = AppEventType::PowerStateChanged;
    event.payload.power_state.revision = snapshot_.revision;
    EventBus::get().publish(event);
}
```

- [ ] **Step 6: Add NetworkService API and implementation**

Create `main/app/services/network_service.h`:

```cpp
#pragma once

#include <stdint.h>

struct NetworkSnapshot {
    bool wifi_connected = false;
    bool sync_in_progress = false;
    bool ntp_synced = false;
    uint32_t revision = 0;
};

class NetworkService {
public:
    static NetworkService& get();
    NetworkSnapshot snapshot();
    void poll();

private:
    NetworkService() = default;
    NetworkSnapshot snapshot_{};
};
```

Create `main/app/services/network_service.cpp`:

```cpp
#include "network_service.h"

#include "app/core/event/event_bus.h"
#include "clock_net.h"

NetworkService& NetworkService::get()
{
    static NetworkService service;
    return service;
}

NetworkSnapshot NetworkService::snapshot()
{
    return snapshot_;
}

void NetworkService::poll()
{
    const ClockNet::Status status = ClockNet::getStatus();
    if (status.wifi_connected == snapshot_.wifi_connected &&
        status.sync_in_progress == snapshot_.sync_in_progress &&
        status.ntp_synced == snapshot_.ntp_synced) {
        return;
    }

    snapshot_.wifi_connected = status.wifi_connected;
    snapshot_.sync_in_progress = status.sync_in_progress;
    snapshot_.ntp_synced = status.ntp_synced;
    ++snapshot_.revision;

    AppEvent event{};
    event.type = AppEventType::NetworkStateChanged;
    event.payload.network_state.revision = snapshot_.revision;
    EventBus::get().publish(event);
}
```

- [ ] **Step 7: Run PowerService test**

Run:

```bash
c++ -std=c++17 -I. -Itests/stubs tests/test_power_service.cpp main/app/core/event/event_bus.cpp -o /tmp/test_power_service && /tmp/test_power_service
```

Expected: command exits `0`.

- [ ] **Step 8: Add service sources to firmware build**

Modify `main/CMakeLists.txt`:

```cmake
       "app/services/time_service.cpp"
       "app/services/power_service.cpp"
       "app/services/network_service.cpp"
```

- [ ] **Step 9: Commit Task 4**

Run:

```bash
git add main/app/services tests/test_power_service.cpp main/CMakeLists.txt
git commit -m "Add core UI snapshot services"
```

---

### Task 5: Clock MVP Migration

**Files:**
- Create: `main/app/features/clock/clock_model.h/cpp`
- Create: `main/app/features/clock/clock_presenter.h/cpp`
- Create: `main/app/features/clock/clock_view.h/cpp`
- Create: `main/app/features/clock/widgets/seven_segment_widget.h/cpp`
- Create: `main/app/screens/clock_screen.h/cpp`
- Create: `tests/test_clock_model.cpp`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Write ClockModel test**

Create `tests/test_clock_model.cpp`:

```cpp
#include "../main/app/features/clock/clock_model.cpp"

#include <stdio.h>
#include <string.h>

int main()
{
    ClockModel model;
    ClockDisplayState invalid = model.buildTime(false, 0, 0, 0, 0, 0, 0);
    if (strcmp(invalid.time, "--:--") != 0 || strcmp(invalid.weekday, "RTC") != 0 || strcmp(invalid.date, "--/--") != 0) {
        printf("invalid RTC display failed\n");
        return 1;
    }

    ClockDisplayState valid = model.buildTime(true, 9, 5, 2, 1, 5, 24);
    if (strcmp(valid.time, "09:05") != 0 || strcmp(valid.weekday, "Mon") != 0 || strcmp(valid.date, "05/24") != 0) {
        printf("valid RTC display failed: %s %s %s\n", valid.time, valid.weekday, valid.date);
        return 1;
    }

    ClockDisplayState blink = model.buildTime(true, 9, 5, 3, 1, 5, 24);
    if (strcmp(blink.time, "09 05") != 0) {
        printf("colon blink failed: %s\n", blink.time);
        return 1;
    }
    return 0;
}
```

- [ ] **Step 2: Run test and verify it fails**

Run:

```bash
c++ -std=c++17 -I. tests/test_clock_model.cpp -o /tmp/test_clock_model && /tmp/test_clock_model
```

Expected: compile failure because `clock_model.cpp` does not exist.

- [ ] **Step 3: Add ClockModel**

Create `main/app/features/clock/clock_model.h`:

```cpp
#pragma once

struct ClockDisplayState {
    char time[6] = "--:--";
    char weekday[4] = "RTC";
    char date[6] = "--/--";
};

struct BatteryDisplayState {
    int percent = -1;
    bool update_label = true;
};

struct NetworkDisplayState {
    bool wifi_connected = false;
    bool sync_in_progress = false;
    bool ntp_synced = false;
    bool external_power = false;
};

class ClockModel {
public:
    ClockDisplayState buildTime(bool rtc_ok, unsigned hour, unsigned minute, unsigned second,
                                unsigned week, unsigned month, unsigned day);
    BatteryDisplayState buildBattery(int percent);
    void resetBattery();

private:
    int battery_disp_pct_ = -1;
};
```

Create `main/app/features/clock/clock_model.cpp`:

```cpp
#include "clock_model.h"

#include <stdio.h>

namespace {
const char* weekdayName(unsigned week)
{
    static const char* kNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return kNames[week % 7];
}

int absInt(int value)
{
    return value < 0 ? -value : value;
}
} // namespace

ClockDisplayState ClockModel::buildTime(bool rtc_ok, unsigned hour, unsigned minute, unsigned second,
                                        unsigned week, unsigned month, unsigned day)
{
    ClockDisplayState state{};
    if (!rtc_ok) {
        return state;
    }

    state.time[0] = static_cast<char>('0' + (hour / 10));
    state.time[1] = static_cast<char>('0' + (hour % 10));
    state.time[2] = (second % 2 == 0) ? ':' : ' ';
    state.time[3] = static_cast<char>('0' + (minute / 10));
    state.time[4] = static_cast<char>('0' + (minute % 10));
    state.time[5] = '\0';
    snprintf(state.weekday, sizeof(state.weekday), "%s", weekdayName(week));
    snprintf(state.date, sizeof(state.date), "%02u/%02u", month, day);
    return state;
}

BatteryDisplayState ClockModel::buildBattery(int percent)
{
    BatteryDisplayState state{};
    state.percent = percent;
    if (percent < 0) {
        battery_disp_pct_ = -1;
        state.update_label = true;
        return state;
    }
    state.update_label = battery_disp_pct_ < 0 || absInt(percent - battery_disp_pct_) >= 5;
    if (state.update_label) {
        battery_disp_pct_ = percent;
    }
    return state;
}

void ClockModel::resetBattery()
{
    battery_disp_pct_ = -1;
}
```

- [ ] **Step 4: Run ClockModel test**

Run:

```bash
c++ -std=c++17 -I. tests/test_clock_model.cpp -o /tmp/test_clock_model && /tmp/test_clock_model
```

Expected: command exits `0`.

- [ ] **Step 5: Split Clock view**

Create `main/app/features/clock/clock_view.h` with LVGL-only methods:

```cpp
#pragma once

#include "clock_model.h"
#include "lvgl.h"

class ClockView {
public:
    void create();
    void destroy();
    void renderTime(const ClockDisplayState& state, bool dimmed);
    void renderBattery(const BatteryDisplayState& state, bool dimmed);
    void renderNetwork(const NetworkDisplayState& state, bool dimmed);

private:
    lv_obj_t* digit_segs_[4][7] = {};
    lv_obj_t* colon_top_ = nullptr;
    lv_obj_t* colon_bottom_ = nullptr;
    lv_obj_t* weekday_label_ = nullptr;
    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* battery_label_ = nullptr;
    lv_obj_t* wifi_icon_ = nullptr;
    lv_obj_t* sync_icon_ = nullptr;
    lv_obj_t* power_icon_ = nullptr;
    lv_obj_t* battery_shell_ = nullptr;
    lv_obj_t* battery_fill_ = nullptr;
    lv_obj_t* battery_cap_ = nullptr;

    void renderDigit(int slot, char c, bool dimmed);
};
```

Create `main/app/features/clock/clock_view.cpp` by moving LVGL widget creation and render helpers from `main/clock_face_screen.cpp`. Keep the same constants, colors, positions, fonts, and segment map. Do not call `i2c_rtc_get()`, `PowerManager::getState()`, or `ClockNet::getStatus()` from this file.

Create `main/app/features/clock/widgets/seven_segment_widget.h`:

```cpp
#pragma once

#include "lvgl.h"

class SevenSegmentWidget {
public:
    void bind(lv_obj_t* segments[7]);
    void render(char value, bool dimmed);

private:
    lv_obj_t* segments_[7] = {};
};
```

Create `main/app/features/clock/widgets/seven_segment_widget.cpp`:

```cpp
#include "seven_segment_widget.h"

namespace {
constexpr uint32_t kInk = 0x22282b;
constexpr uint32_t kDimInk = 0x363b3d;

constexpr bool kDigitMap[10][7] = {
    {true,  true,  true,  false, true,  true,  true },
    {false, false, true,  false, false, true,  false},
    {true,  false, true,  true,  true,  false, true },
    {true,  false, true,  true,  false, true,  true },
    {false, true,  true,  true,  false, true,  false},
    {true,  true,  false, true,  false, true,  true },
    {true,  true,  false, true,  true,  true,  true },
    {true,  false, true,  false, false, true,  false},
    {true,  true,  true,  true,  true,  true,  true },
    {true,  true,  true,  true,  false, true,  true },
};
} // namespace

void SevenSegmentWidget::bind(lv_obj_t* segments[7])
{
    for (int i = 0; i < 7; ++i) {
        segments_[i] = segments[i];
    }
}

void SevenSegmentWidget::render(char value, bool dimmed)
{
    for (int i = 0; i < 7; ++i) {
        bool active = false;
        if (value >= '0' && value <= '9') {
            active = kDigitMap[value - '0'][i];
        } else if (value == '-') {
            active = i == 3;
        }
        if (segments_[i]) {
            lv_obj_set_style_bg_color(segments_[i], lv_color_hex(dimmed ? kDimInk : kInk), 0);
            lv_obj_set_style_bg_opa(segments_[i], active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        }
    }
}
```

- [ ] **Step 6: Add ClockPresenter**

Create `main/app/features/clock/clock_presenter.h`:

```cpp
#pragma once

#include "clock_model.h"
#include "clock_view.h"

class ClockPresenter {
public:
    explicit ClockPresenter(ClockView& view);
    void start();
    void stop();
    void tick();

private:
    ClockView& view_;
    ClockModel model_;
    bool running_ = false;
    bool dimmed_ = false;
    bool external_power_ = false;
    uint32_t last_time_revision_ = 0;
    uint32_t last_power_revision_ = 0;
    uint32_t last_network_revision_ = 0;

    void renderAll();
};
```

Create `main/app/features/clock/clock_presenter.cpp`:

```cpp
#include "clock_presenter.h"

#include "app/core/event/event_bus.h"
#include "app/services/network_service.h"
#include "app/services/power_service.h"
#include "app/services/time_service.h"

ClockPresenter::ClockPresenter(ClockView& view) : view_(view) {}

void ClockPresenter::start()
{
    running_ = true;
    model_.resetBattery();
    renderAll();
}

void ClockPresenter::stop()
{
    running_ = false;
}

void ClockPresenter::tick()
{
    if (!running_) {
        return;
    }

    TimeService::get().poll();
    PowerService::get().poll();
    NetworkService::get().poll();

    bool dirty = false;
    AppEvent event{};
    while (EventBus::get().poll(&event)) {
        if (event.type == AppEventType::ClockTimeChanged &&
            event.payload.clock_time.revision != last_time_revision_) {
            last_time_revision_ = event.payload.clock_time.revision;
            dirty = true;
        } else if (event.type == AppEventType::PowerStateChanged &&
                   event.payload.power_state.revision != last_power_revision_) {
            last_power_revision_ = event.payload.power_state.revision;
            dirty = true;
        } else if (event.type == AppEventType::NetworkStateChanged &&
                   event.payload.network_state.revision != last_network_revision_) {
            last_network_revision_ = event.payload.network_state.revision;
            dirty = true;
        }
    }

    if (dirty) {
        renderAll();
    }
}

void ClockPresenter::renderAll()
{
    const ClockSnapshot clock = TimeService::get().snapshot();
    const PowerSnapshot power = PowerService::get().snapshot();
    const NetworkSnapshot network = NetworkService::get().snapshot();

    dimmed_ = power.dimmed;
    external_power_ = power.external_power;

    const ClockDisplayState time = model_.buildTime(clock.rtc_ok, clock.hour, clock.minute, clock.second,
                                                    clock.week, clock.month, clock.day);
    BatteryDisplayState battery = model_.buildBattery(power.battery_percent);
    NetworkDisplayState net{};
    net.wifi_connected = network.wifi_connected;
    net.sync_in_progress = network.sync_in_progress;
    net.ntp_synced = network.ntp_synced;
    net.external_power = external_power_;

    view_.renderTime(time, dimmed_);
    view_.renderBattery(battery, dimmed_);
    view_.renderNetwork(net, dimmed_);
}
```

- [ ] **Step 7: Add ClockScreen**

Create `main/app/screens/clock_screen.h`:

```cpp
#pragma once

#include "app/features/clock/clock_presenter.h"
#include "app/features/clock/clock_view.h"
#include "screen.h"

class ClockScreen : public Screen {
public:
    ClockScreen();
    void onEnter() override;
    void onExit() override;
    void onTick() override;

private:
    ClockView view_;
    ClockPresenter presenter_;
};
```

Create `main/app/screens/clock_screen.cpp`:

```cpp
#include "clock_screen.h"

ClockScreen::ClockScreen() : presenter_(view_) {}

void ClockScreen::onEnter()
{
    view_.create();
    presenter_.start();
}

void ClockScreen::onExit()
{
    presenter_.stop();
    view_.destroy();
}

void ClockScreen::onTick()
{
    presenter_.tick();
}
```

- [ ] **Step 8: Add clock sources to firmware build**

Modify `main/CMakeLists.txt`:

```cmake
       "app/features/clock/clock_model.cpp"
       "app/features/clock/clock_presenter.cpp"
       "app/features/clock/clock_view.cpp"
       "app/features/clock/widgets/seven_segment_widget.cpp"
       "app/screens/clock_screen.cpp"
```

- [ ] **Step 9: Build**

Run:

```bash
cmake --build build
```

Expected: firmware builds.

- [ ] **Step 10: Commit Task 5**

Run:

```bash
git add main/app/features/clock main/app/screens/clock_screen.* tests/test_clock_model.cpp main/CMakeLists.txt
git commit -m "Split clock screen into MVP components"
```

---

### Task 6: ScreenManager Integration

**Files:**
- Create: `main/app/screens/screen_manager.h`
- Create: `main/app/screens/screen_manager.cpp`
- Create: `main/app/screens/music_screen.h`
- Create: `main/app/screens/music_screen.cpp`
- Modify: `main/main.cpp`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Add ScreenManager interface**

Create `main/app/screens/screen_manager.h`:

```cpp
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

    void detachGestureHandler();
    void switchTo(ScreenId target);
    void handleSwipe(SwipeDirection swipe);

    ScreenId current_ = ScreenId::Clock;
    ClockScreen clock_;
    MusicScreen music_;
    SwipeGestureDetector swipe_detector_;
    lv_obj_t* gesture_root_ = nullptr;
    lv_timer_t* tick_timer_ = nullptr;
};
```

- [ ] **Step 2: Add ScreenManager implementation**

Create `main/app/screens/screen_manager.cpp` by moving the current logic from `main/screen_manager.cpp`. Preserve `enableGestureEvents()` and `currentTouchPoint()`. Replace `create()` calls with `onEnter()`, `destroy()` calls with `onExit()`, and add:

```cpp
void ScreenManager::tick()
{
    if (current_ == ScreenId::Clock) {
        clock_.onTick();
    } else {
        music_.onTick();
    }
}

void ScreenManager::onTickTimer(lv_timer_t* timer)
{
    auto* manager = static_cast<ScreenManager*>(timer->user_data);
    if (manager) {
        manager->tick();
    }
}
```

In `create()`, start the LVGL timer:

```cpp
tick_timer_ = lv_timer_create(onTickTimer, 1000, this);
```

In `destroy()`, delete it:

```cpp
if (tick_timer_) {
    lv_timer_del(tick_timer_);
    tick_timer_ = nullptr;
}
```

- [ ] **Step 3: Wire main.cpp to the new manager**

Modify `main/main.cpp` include:

```cpp
#include "app/screens/screen_manager.h"
```

Remove:

```cpp
#include "screen_manager.h"
```

Keep:

```cpp
if (LvglPort::Guard g; g) {
    ScreenManager::instance().create();
}
```

- [ ] **Step 4: Add a legacy MusicScreen adapter**

Create `main/app/screens/music_screen.h`:

```cpp
#pragma once

#include "music_player_screen.h"
#include "screen.h"

class MusicScreen : public Screen {
public:
    void onEnter() override;
    void onExit() override;
    void onTick() override;

private:
    MusicPlayerScreen legacy_;
};
```

Create `main/app/screens/music_screen.cpp`:

```cpp
#include "music_screen.h"

void MusicScreen::onEnter()
{
    legacy_.create();
}

void MusicScreen::onExit()
{
    legacy_.destroy();
}

void MusicScreen::onTick()
{
}
```

This adapter lets the new `ScreenManager` own both screen lifecycles while the full Music MVP split happens in Task 8. Task 8 replaces the adapter internals with `MusicView` and `MusicPresenter`.

- [ ] **Step 5: Add screen manager sources**

Modify `main/CMakeLists.txt`:

```cmake
       "app/screens/screen_manager.cpp"
       "app/screens/music_screen.cpp"
```

- [ ] **Step 6: Build**

Run:

```bash
cmake --build build
```

Expected: firmware builds with the new `ScreenManager`, new Clock MVP screen, and legacy-adapted Music screen.

- [ ] **Step 7: Commit Task 6**

Run:

```bash
git add main/app/screens/screen_manager.* main/app/screens/music_screen.* main/main.cpp main/CMakeLists.txt
git commit -m "Add MVP screen manager lifecycle"
```

---

### Task 7: Music State, MQTT Service, And Cover Service

**Files:**
- Create: `main/app/features/music/music_state.h`
- Create: `main/app/services/mqtt_service.h/cpp`
- Create: `main/app/services/cover_service.h/cpp`
- Create: `tests/test_mqtt_service_music_parse.cpp`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Write parsing test**

Create `tests/test_mqtt_service_music_parse.cpp`:

```cpp
#include "../main/app/services/mqtt_service.cpp"

#include <stdio.h>
#include <string.h>

int main()
{
    MusicState state{};
    applyShairportField(state, "title", "Live Track", 10);
    applyShairportField(state, "artist", "Artist", 6);
    applyShairportField(state, "active", "1", 1);
    applyShairportField(state, "playing", "0", 1);
    applyShairportField(state, "ssnc/prgr", "1000/1500/2000", 14);

    if (strcmp(state.title, "Live Track") != 0 || strcmp(state.artist, "Artist") != 0) {
        printf("metadata parse failed\n");
        return 1;
    }
    if (!state.active || state.playing) {
        printf("boolean parse failed\n");
        return 1;
    }
    if (state.progress_start_frame != 1000 || state.progress_current_frame != 1500 || state.progress_end_frame != 2000) {
        printf("progress parse failed\n");
        return 1;
    }
    return 0;
}
```

- [ ] **Step 2: Run parsing test and verify it fails**

Run:

```bash
c++ -std=c++17 -I. tests/test_mqtt_service_music_parse.cpp main/app/core/event/event_bus.cpp -o /tmp/test_mqtt_service_music_parse && /tmp/test_mqtt_service_music_parse
```

Expected: compile failure because `mqtt_service.cpp` does not exist.

- [ ] **Step 3: Add header-only MusicState**

Create `main/app/features/music/music_state.h`:

```cpp
#pragma once

#include <stdint.h>

struct MusicState {
    bool active = false;
    bool playing = false;

    char title[96] = "Make a Shadow";
    char artist[96] = "Meg Myers";
    char album[96] = "Sorry";
    char genre[48] = "";

    int volume_percent = 64;

    uint32_t progress_start_frame = 1174943435u;
    uint32_t progress_current_frame = 1181140317u;
    uint32_t progress_end_frame = 1182711473u;
    uint32_t last_progress_ms = 0;
    uint32_t revision = 0;
};
```

- [ ] **Step 4: Add MqttService interface and parsing helper**

Create `main/app/services/mqtt_service.h`:

```cpp
#pragma once

#include "app/features/music/music_state.h"

#include <stddef.h>

class MqttService {
public:
    static MqttService& get();
    void init();
    MusicState snapshot();
    bool takeCover(uint8_t** data, uint32_t* size);
};

void applyShairportField(MusicState& state, const char* field, const char* payload, size_t payload_len);
```

Create `main/app/services/mqtt_service.cpp` by moving parsing logic from `main/music_state.cpp` and snapshot access from `main/music_mqtt.cpp`. Keep the MQTT socket implementation unchanged for this task. When a non-cover field changes, increment `MusicState::revision` and publish:

```cpp
AppEvent event{};
event.type = AppEventType::MusicStateChanged;
event.payload.music_state.revision = state.revision;
EventBus::get().publish(event);
```

- [ ] **Step 5: Run parsing test**

Run:

```bash
c++ -std=c++17 -I. tests/test_mqtt_service_music_parse.cpp main/app/core/event/event_bus.cpp -o /tmp/test_mqtt_service_music_parse && /tmp/test_mqtt_service_music_parse
```

Expected: command exits `0`.

- [ ] **Step 6: Add CoverService skeleton**

Create `main/app/services/cover_service.h`:

```cpp
#pragma once

#include "app/core/event/app_events.h"
#include "lvgl.h"

#include <stdint.h>

struct CoverBuffer {
    uint32_t cover_id = 0;
    CoverStatus status = CoverStatus::Idle;
    lv_img_dsc_t image{};
    lv_color_t* pixels = nullptr;
};

class CoverService {
public:
    static CoverService& get();
    uint32_t acceptJpeg(uint8_t* data, uint32_t size);
    CoverBuffer active();
    void clear();

private:
    CoverService() = default;
    uint32_t next_cover_id_ = 0;
    CoverBuffer active_{};
};
```

Create `main/app/services/cover_service.cpp` with the first-pass single-entry behavior:

```cpp
#include "cover_service.h"

#include "app/core/event/event_bus.h"

CoverService& CoverService::get()
{
    static CoverService service;
    return service;
}

uint32_t CoverService::acceptJpeg(uint8_t*, uint32_t)
{
    active_.cover_id = ++next_cover_id_;
    active_.status = CoverStatus::Loading;

    AppEvent event{};
    event.type = AppEventType::CoverStateChanged;
    event.payload.cover_state.cover_id = active_.cover_id;
    event.payload.cover_state.status = active_.status;
    EventBus::get().publish(event);
    return active_.cover_id;
}

CoverBuffer CoverService::active()
{
    return active_;
}

void CoverService::clear()
{
    active_ = CoverBuffer{};
}
```

- [ ] **Step 7: Add service sources to build**

Modify `main/CMakeLists.txt`:

```cmake
       "app/services/mqtt_service.cpp"
       "app/services/cover_service.cpp"
```

- [ ] **Step 8: Commit Task 7**

Run:

```bash
git add main/app/features/music/music_state.h main/app/services/mqtt_service.* main/app/services/cover_service.* tests/test_mqtt_service_music_parse.cpp main/CMakeLists.txt
git commit -m "Add music services and state snapshot"
```

---

### Task 8: Music MVP Migration

**Files:**
- Create: `main/app/features/music/music_model.h/cpp`
- Create: `main/app/features/music/music_presenter.h/cpp`
- Create: `main/app/features/music/music_view.h/cpp`
- Create: `main/app/features/music/widgets/cover_widget.h/cpp`
- Create: `main/app/features/music/widgets/visualizer_widget.h/cpp`
- Create: `main/app/screens/music_screen.h/cpp`
- Modify: `sim/main.cpp`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Add MusicModel from existing pure helpers**

Create `main/app/features/music/music_model.h`:

```cpp
#pragma once

#include "music_state.h"

#include <stdint.h>

struct MusicDisplayState {
    char title[96] = "";
    char subtitle[200] = "";
    char time[24] = "";
    uint32_t progress_per_mille = 0;
    bool playing = false;
};

class MusicModel {
public:
    MusicDisplayState build(const MusicState& state, uint32_t elapsed_frames);
};
```

Create `main/app/features/music/music_model.cpp` using the existing formatting behavior from `music_player_screen.cpp` and `music_time_format.h`.

- [ ] **Step 2: Move visualizer widget**

Create `main/app/features/music/widgets/visualizer_widget.h`:

```cpp
#pragma once

#include "lvgl.h"

class VisualizerWidget {
public:
    static constexpr int kBarCount = 44;
    void create(lv_obj_t* parent);
    void render(uint32_t progress_per_mille, bool playing);
    void clear();

private:
    lv_obj_t* bars_[kBarCount] = {};
};
```

Create `main/app/features/music/widgets/visualizer_widget.cpp` by moving spectrum-bar creation and update logic from `main/music_player_screen.cpp`. Continue using `musicVisualizerBarHeight()`.

- [ ] **Step 3: Move cover widget**

Create `main/app/features/music/widgets/cover_widget.h`:

```cpp
#pragma once

#include "app/services/cover_service.h"
#include "lvgl.h"

class CoverWidget {
public:
    void create(lv_obj_t* parent);
    void renderPlaceholder();
    void renderCover(const CoverBuffer& cover);
    void clear();

private:
    lv_obj_t* cover_img_ = nullptr;
    lv_obj_t* cover_band_ = nullptr;
    lv_obj_t* cover_accent_ = nullptr;
};
```

Create `cover_widget.cpp` by moving cover placeholder and `lv_img_set_src()` behavior from `main/music_player_screen.cpp`. Do not decode JPEGs in the widget.

- [ ] **Step 4: Move music fonts**

Create `main/app/ui/fonts/music_fonts.h`:

```cpp
#pragma once

#include "lvgl.h"

const lv_font_t* musicTextFont();
const lv_font_t* musicSmallTextFont();
```

Create `main/app/ui/fonts/music_fonts.cpp` by moving `createMusicFont()`, `musicTextFont()`, and `musicSmallTextFont()` from `main/music_player_screen.cpp`.

- [ ] **Step 5: Add MusicView**

Create `main/app/features/music/music_view.h`:

```cpp
#pragma once

#include "music_model.h"
#include "widgets/cover_widget.h"
#include "widgets/visualizer_widget.h"
#include "lvgl.h"

class MusicView {
public:
    void create();
    void destroy();
    void render(const MusicDisplayState& state);
    void renderCover(const CoverBuffer& cover);

private:
    lv_obj_t* title_ = nullptr;
    lv_obj_t* subtitle_ = nullptr;
    lv_obj_t* time_ = nullptr;
    lv_obj_t* play_pause_icon_ = nullptr;
    CoverWidget cover_;
    VisualizerWidget visualizer_;
};
```

Create `music_view.cpp` by moving layout construction from `main/music_player_screen.cpp`. Preserve colors, positions, fonts, and LVGL object sizes. Do not call `MusicMqtt`, `CoverService`, or `EventBus`.

- [ ] **Step 6: Add MusicPresenter**

Create `main/app/features/music/music_presenter.h`:

```cpp
#pragma once

#include "music_model.h"
#include "music_view.h"

class MusicPresenter {
public:
    explicit MusicPresenter(MusicView& view);
    void start();
    void stop();
    void tick();

private:
    MusicView& view_;
    MusicModel model_;
    bool running_ = false;
    uint32_t last_music_revision_ = 0;
    uint32_t last_cover_id_ = 0;
    void renderMusic();
    void renderCover();
};
```

Create `music_presenter.cpp` to read `MqttService::snapshot()`, `CoverService::active()`, handle `MusicStateChanged`, `CoverStateChanged`, and matching `FeatureAction` events, then call `MusicView`.

- [ ] **Step 7: Replace legacy MusicScreen adapter**

Replace `main/app/screens/music_screen.h` with:

```cpp
#pragma once

#include "app/features/music/music_presenter.h"
#include "app/features/music/music_view.h"
#include "screen.h"

class MusicScreen : public Screen {
public:
    MusicScreen();
    void onEnter() override;
    void onExit() override;
    void onTick() override;

private:
    MusicView view_;
    MusicPresenter presenter_;
};
```

Replace `main/app/screens/music_screen.cpp` with:

```cpp
#include "music_screen.h"

MusicScreen::MusicScreen() : presenter_(view_) {}

void MusicScreen::onEnter()
{
    view_.create();
    presenter_.start();
}

void MusicScreen::onExit()
{
    presenter_.stop();
    view_.destroy();
}

void MusicScreen::onTick()
{
    presenter_.tick();
}
```

- [ ] **Step 8: Update simulator to use new MusicScreen**

Modify `sim/main.cpp`:

```cpp
#include "app/screens/music_screen.h"
```

Replace:

```cpp
MusicPlayerScreen screen;
screen.create();
```

with:

```cpp
MusicScreen screen;
screen.onEnter();
```

Replace `screen.destroy();` with `screen.onExit();`.

- [ ] **Step 9: Add music sources to build**

Modify `main/CMakeLists.txt`:

```cmake
       "app/features/music/music_model.cpp"
       "app/features/music/music_presenter.cpp"
       "app/features/music/music_view.cpp"
       "app/features/music/widgets/cover_widget.cpp"
       "app/features/music/widgets/visualizer_widget.cpp"
       "app/ui/fonts/music_fonts.cpp"
       "app/screens/music_screen.cpp"
```

- [ ] **Step 10: Build simulator and firmware**

Run:

```bash
cmake -S sim -B build-sim
cmake --build build-sim --target music_ui_sim
cmake --build build
```

Expected: simulator and firmware build.

- [ ] **Step 11: Commit Task 8**

Run:

```bash
git add main/app/features/music main/app/ui/fonts main/app/screens/music_screen.* sim/main.cpp main/CMakeLists.txt
git commit -m "Split music screen into MVP components"
```

---

### Task 9: Remove Old UI Entry Points

**Files:**
- Modify: `main/CMakeLists.txt`
- Modify: `main/main.cpp`
- Delete or stop building: `main/clock_face_screen.*`, `main/music_player_screen.*`, `main/screen_manager.*`, `main/screen_nav.*`, `main/screen.h`, `main/music_state.*`

- [ ] **Step 1: Stop building old sources**

Modify `main/CMakeLists.txt` and remove:

```cmake
       "clock_face_screen.cpp"
       "music_player_screen.cpp"
       "screen_manager.cpp"
       "screen_nav.cpp"
       "music_state.cpp"
```

Keep shared pure helpers that are still used, such as `music_background.cpp`, `music_visualizer.cpp`, `music_time_format.h`, and icon/asset C files.

- [ ] **Step 2: Update includes**

Search:

```bash
rg -n '#include "(clock_face_screen|music_player_screen|screen_manager|screen_nav|screen|music_state)\\.h"' main sim tests
```

Replace old includes with new `app/...` includes. The expected remaining matches after the change are zero for old screen headers.

- [ ] **Step 3: Run pure tests**

Run:

```bash
c++ -std=c++17 -I. tests/test_event_queue.cpp -o /tmp/test_event_queue && /tmp/test_event_queue
c++ -std=c++17 -I. tests/test_event_bus.cpp -o /tmp/test_event_bus && /tmp/test_event_bus
c++ -std=c++17 -I. tests/test_screen_nav.cpp main/app/core/event/event_bus.cpp -o /tmp/test_screen_nav && /tmp/test_screen_nav
c++ -std=c++17 -I. tests/test_clock_model.cpp -o /tmp/test_clock_model && /tmp/test_clock_model
c++ -std=c++17 -I. tests/test_mqtt_service_music_parse.cpp main/app/core/event/event_bus.cpp -o /tmp/test_mqtt_service_music_parse && /tmp/test_mqtt_service_music_parse
```

Expected: all commands exit `0`.

- [ ] **Step 4: Build firmware**

Run:

```bash
cmake --build build
```

Expected: firmware builds.

- [ ] **Step 5: Commit Task 9**

Run:

```bash
git add main sim tests
git commit -m "Switch firmware to MVP EventBus UI"
```

---

### Task 10: Final Verification

**Files:**
- Modify only if verification exposes a defect.

- [ ] **Step 1: Run all pure tests**

Run:

```bash
c++ -std=c++17 -I. tests/test_event_queue.cpp -o /tmp/test_event_queue && /tmp/test_event_queue
c++ -std=c++17 -I. tests/test_event_bus.cpp -o /tmp/test_event_bus && /tmp/test_event_bus
c++ -std=c++17 -I. tests/test_screen_nav.cpp main/app/core/event/event_bus.cpp -o /tmp/test_screen_nav && /tmp/test_screen_nav
c++ -std=c++17 -I. tests/test_clock_model.cpp -o /tmp/test_clock_model && /tmp/test_clock_model
c++ -std=c++17 -I. tests/test_mqtt_service_music_parse.cpp main/app/core/event/event_bus.cpp -o /tmp/test_mqtt_service_music_parse && /tmp/test_mqtt_service_music_parse
```

Expected: all commands exit `0`.

- [ ] **Step 2: Build firmware**

Run:

```bash
cmake --build build
```

Expected: firmware builds.

- [ ] **Step 3: Build simulator**

Run:

```bash
cmake -S sim -B build-sim
cmake --build build-sim --target music_ui_sim
```

Expected: simulator builds.

- [ ] **Step 4: Run simulator screenshot check**

Run:

```bash
build-sim/music_ui_sim --offline --run-ms 1200 --screenshot /tmp/music-ui-mvp.bmp
```

Expected: command exits `0` and writes `/tmp/music-ui-mvp.bmp`.

- [ ] **Step 5: Hardware smoke test**

Run:

```bash
ESPPORT=/dev/cu.usbmodem111401 IDF_PATH=/Users/xinzlong/.espressif/v6.0.1/esp-idf cmake --build build --target flash
```

Expected: device flashes successfully.

Manual checks:

- Device boots to Clock.
- Clock time/date/status/battery/dimming behavior matches the pre-refactor UI.
- Left swipe switches to Music.
- Right swipe switches back to Clock.
- Music title/subtitle/progress/visualizer/play-pause/cover behavior matches the pre-refactor UI.

- [ ] **Step 6: Commit verification fixes when files changed**

When Step 1 through Step 5 changed files, commit them:

```bash
git add main sim tests
git commit -m "Verify MVP EventBus UI migration"
```

When Step 1 through Step 5 did not change files, do not create an empty commit.
