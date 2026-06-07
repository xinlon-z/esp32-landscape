# MVP + EventBus UI Architecture Design

## Goal

Convert the current LVGL UI code to a unified Model-View-Presenter architecture with a lightweight EventBus.

The refactor covers both existing screens:

- Clock
- Music player

The visual design and user-visible behavior must remain unchanged. This is an architecture refactor, not a UI redesign.

## Architecture Decision Record

### ADR 1: EventBus Carries Lightweight Notifications Only

Decision: `EventBus` events must not carry large objects such as JPEG payloads, decoded cover pixels, LVGL image descriptors, or long strings.

Services own the latest snapshot and heavy buffers. Events only announce that something changed, with small identifiers or status values.

Reason:

- Keeps the event queue small and deterministic.
- Avoids PSRAM pressure and heap fragmentation from event copies.
- Lets presenters read the latest state when a screen is entered, even if the original change event was missed.

Example:

```cpp
#include <cstdint>
#include <type_traits>

enum class AppEventType {
    ClockTimeChanged,
    PowerStateChanged,
    NetworkStateChanged,
    MusicStateChanged,
    CoverStateChanged,
    FeatureAction,
};

struct MusicStateChangedEvent {
    uint32_t revision;
};

struct CoverStateChangedEvent {
    uint32_t cover_id;
    CoverStatus status;
};

struct FeatureActionEvent {
    uint8_t screen_id;
    uint8_t action_id;
};

struct AppEvent {
    AppEventType type;
    union Payload {
        ClockTimeChangedEvent clock_time;
        PowerStateChangedEvent power_state;
        NetworkStateChangedEvent network_state;
        MusicStateChangedEvent music_state;
        CoverStateChangedEvent cover_state;
        FeatureActionEvent feature_action;
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

Requirement:

- Every `AppEvent` payload type must be trivially copyable.
- `AppEvent` must be trivially copyable.
- Payload types must not contain `std::string`, heap-owning containers, LVGL object pointers, or ownership-bearing handles.
- Event processing must switch on `AppEventType` before reading the corresponding union member.

### ADR 2: Services Own Snapshots

Decision: every service that adapts external state owns the latest snapshot.

Examples:

- `TimeService` owns the latest clock snapshot.
- `PowerService` owns the latest power snapshot.
- `NetworkService` owns the latest WiFi/NTP snapshot.
- `MqttService` owns the latest MQTT connection state and music snapshot.
- `CoverService` owns cover loading state and cover buffers.

Presenters read snapshots on `start()` and then consume lightweight events on `tick()`.

Snapshot access rule:

- Services expose snapshot copy APIs, not mutable references.
- Snapshot reads are protected by the service's mutex, atomic packed state, or equivalent thread-safe mechanism.
- Presenters receive a copy and never keep pointers into service-owned snapshot storage.

Concrete service mechanisms:

| Service | Snapshot mechanism |
|---|---|
| `TimeService` | mutex + `ClockSnapshot` copy |
| `PowerService` | `std::atomic<uint32_t>` packed state, matching the existing `PowerManager` pattern |
| `NetworkService` | mutex + `NetworkSnapshot` copy |
| `MqttService` | mutex + `MusicState` copy and atomic revision |
| `CoverService` | mutex + fixed cover entry table keyed by `cover_id` |

Revision rule:

- Each service that publishes snapshot-change events owns a `uint32_t revision`.
- The service increments `revision` every time it commits a new snapshot.
- The event payload carries the committed revision.
- The presenter stores `last_seen_revision`.
- During `tick()`, if an event revision differs from `last_seen_revision`, the presenter reads one fresh snapshot copy and updates `last_seen_revision`.
- Multiple queued events for the same snapshot type collapse to one render pass because the service snapshot is authoritative.

Reason:

- Avoids sticky-event complexity in the EventBus.
- Makes screen re-entry deterministic.
- Allows EventBus queue depth to stay small, around 8 to 16 events.

### ADR 3: Presenter Consumes Events Only In UI Tick

Decision: presenters must consume EventBus events only from the UI tick while the LVGL lock is held.

This is a mandatory thread-safety rule. MQTT, power, network, and cover tasks must never call presenter or view methods directly.

Definition:

- UI tick means `Screen::onTick()`.
- `ScreenManager` calls `Screen::onTick()` from the LVGL render/timer path.
- The LVGL lock is already held when `Screen::onTick()` runs.

Forbidden call paths:

```text
MQTT message callback  -> presenter.tick() or presenter render method
FreeRTOS timer callback -> presenter.tick() or presenter render method
Power task or ISR       -> presenter method or view method
Cover decode task       -> presenter method or view method
Service callback        -> LVGL object mutation
```

Reason:

- LVGL updates must stay on the UI side of the system.
- Keeps cross-task communication limited to services and the EventBus queue.
- Prevents callbacks from updating deleted or inactive screen widgets.

### ADR 4: View Never Subscribes To EventBus

Decision: views do not subscribe to `EventBus`, call services, or know about external event flow.

Views only expose render methods used by presenters.

Allowed interface shape:

```cpp
class ClockView {
public:
    void setTime(const ClockDisplayState& state);
    void setBattery(const BatteryDisplayState& state);
    void setNetwork(const NetworkDisplayState& state);
    void setDimmed(bool dimmed);
};
```

Forbidden view behavior:

- No service pointers or references.
- No calls to `EventBus::publish()` or `EventBus::poll()`.
- No calls into MQTT, RTC, power, network, or cover services.
- No business decisions based on cached previous business state.

Views may cache the last display state only for local redraw optimization. That cache must not become feature logic.

Reason:

- Keeps LVGL widget code local and testable through presenter output.
- Prevents global events from leaking into visual layout code.
- Makes it safe to redesign a view without changing service code.

### ADR 5: `music_state` And `music_model` Have Separate Responsibilities

Decision: `music_state.h` defines shared plain data types. `music_model.*` defines UI-thread view-model logic for the music feature.

`music_state.h`:

- Plain data structures and enums.
- Trivially copyable where practical.
- Shared by services, events, and presenters.
- Does not include `music_model.h`.
- Header-only. There is no `music_state.cpp`.
- Contains no parsing, formatting, service access, LVGL code, or ownership-bearing fields.

`music_model.*`:

- Presenter-owned feature model.
- Exists only on the UI thread.
- Is owned by `MusicPresenter`.
- Formats and derives presentation state such as subtitle text, progress value, time labels, and visualizer render values.
- Holds display-state fields such as formatted elapsed time, formatted duration, progress ratio, visualizer visibility, play/pause display state, and cover display state.
- Represents cover display state as loading, ready, error, or placeholder plus `cover_id` when applicable.
- Is updated only by `MusicPresenter` after snapshot reads or relevant event consumption.
- Can include `music_state.h`.
- Is not included by services.
- Does not include service headers.

Required dependency direction:

```text
music_model -> music_state
services    -> music_state
music_state -> no feature model
```

Reason:

- Prevents circular dependencies.
- Keeps shared domain state separate from screen-specific presentation state.
- Makes service snapshots reusable outside the current music screen.

### ADR 6: Screen Owns Presenter And View

Decision: each screen object owns its presenter and view.

Pattern:

```cpp
class ClockScreen : public Screen {
public:
    void onEnter() override { view_.create(); presenter_.start(); }
    void onExit() override { presenter_.stop(); view_.destroy(); }
    void onTick() override { presenter_.tick(); }

private:
    ClockView view_;
    ClockPresenter presenter_{view_};
};
```

`Screen` is responsible for lifecycle. `Presenter` is responsible for feature logic. `View` is responsible for LVGL widgets and rendering.

Reason:

- Keeps navigation separate from feature logic.
- Gives each screen one clear owner for objects with LVGL lifetimes.
- Makes `Screen::onTick()` the only screen entry point for event polling and LVGL rendering.

### ADR 7: Gesture Routing Is Split By Responsibility

Decision: navigation gestures are handled by the screen layer, while feature gestures go through EventBus.

Routing:

```text
Left/right screen switching gesture
    -> GestureManager
    -> ScreenManager
    -> active Screen transition

Feature-specific gesture or button intent
    -> GestureManager or View callback
    -> EventBus
    -> active Presenter on UI tick
```

Reason:

- Screen switching remains a navigation concern.
- Presenters can still own feature behavior such as play/pause, future lyrics panels, or settings actions.
- Avoids making `ScreenManager` subscribe to business events.

### ADR 8: UI EventBus Is Not A Service-To-Service Bus

Decision: the EventBus defined in this spec is the UI EventBus. It is a service-to-UI notification path only.

Services publish UI events but do not consume the UI EventBus.

Service coordination rules for the first refactor:

- `MqttService` keeps its current reconnect behavior and does not subscribe to `NetworkStateChanged`.
- `MqttService` transfers cover payload ownership through an explicit `CoverService` API because cover payloads are heavy and cannot be copied through the UI EventBus.
- `PowerService`, `TimeService`, and `NetworkService` do not depend on feature presenters or views.
- Services must not poll the UI EventBus.

Reason:

- Keeps EventBus consumption on one thread path: `Screen::onTick()`.
- Avoids introducing a second service-side event loop and its locking rules.
- Reduces migration risk while the current firmware already has working task ownership in `ClockNet`, `PowerManager`, and `MusicMqtt`.

Future option:

- A separate `ServiceEventBus` can be introduced later if service-to-service decoupling becomes necessary.
- `ServiceEventBus` must use independent subscriber channels or independent ring buffers per service consumer.
- `ServiceEventBus` must not share the UI EventBus queue or UI tick drain path.
- Adding service-side event consumption requires its own threading contract and tests.

### ADR 9: CoverService Uses An Async State Machine

Decision: cover image work is owned by `CoverService`, not by the music presenter or view.

State machine:

```text
IDLE -> LOADING -> READY
               -> ERROR
```

Event payload:

```cpp
struct CoverStateChangedEvent {
    uint32_t cover_id;
    CoverStatus status;
};
```

Ownership:

- `CoverService` owns JPEG bytes, decoded pixels, and PSRAM buffers.
- `MusicView` may receive a borrowed cover handle or buffer pointer while the active cover remains valid.
- `MusicView` does not free cover memory.
- `MusicPresenter` decides when to show placeholder, loading, ready, or fallback state.

External behavior:

- When new cover data arrives, `CoverService` assigns a new `cover_id`, sets status to `LOADING`, starts background decode or transform work, and publishes `CoverStateChangedEvent {cover_id, LOADING}`.
- When decode succeeds, `CoverService` writes the buffer to PSRAM-backed storage, sets status to `READY`, and publishes `CoverStateChangedEvent {cover_id, READY}`.
- When decode fails, `CoverService` sets status to `ERROR` and publishes `CoverStateChangedEvent {cover_id, ERROR}`.
- When status is `READY`, the active cover buffer pointer remains valid until the next `cover_id` replaces it.
- Views borrow cover buffers by `cover_id`; they do not own or free them.

Cover ID and cache rule:

- `cover_id` is a monotonically increasing `uint32_t` counter owned by `CoverService`.
- `CoverService` increments `cover_id` each time it accepts a new cover payload from `MqttService`, even if the bytes match the previous cover.
- The first refactor uses exactly one active cover entry: the most recently accepted cover.
- Previous cover buffers are released when a new cover payload is accepted.
- Content-hash deduplication and multi-cover caching are out of scope for the first refactor.

Reason:

- Downloading, decoding, and background generation cannot run in UI tick.
- Memory ownership must be centralized to avoid double-free and stale LVGL image sources.
- Presenter remains focused on presentation decisions.

## Target Directory Structure

The refactor introduces a unified application directory under `main/app`.

```text
main/
  app/
    core/
      app/
        app_init.cpp
        app_init.h
      event/
        app_events.h
        event_bus.cpp
        event_bus.h
        event_queue.h
      utils/
        time_format.h

    services/
      mqtt_service.cpp
      mqtt_service.h
      time_service.cpp
      time_service.h
      power_service.cpp
      power_service.h
      network_service.cpp
      network_service.h
      cover_service.cpp
      cover_service.h

    features/
      clock/
        clock_model.cpp
        clock_model.h
        clock_presenter.cpp
        clock_presenter.h
        clock_view.cpp
        clock_view.h
        widgets/
          seven_segment_widget.cpp
          seven_segment_widget.h

      music/
        music_state.h
        music_model.cpp
        music_model.h
        music_presenter.cpp
        music_presenter.h
        music_view.cpp
        music_view.h
        widgets/
          cover_widget.cpp
          cover_widget.h
          visualizer_widget.cpp
          visualizer_widget.h

    screens/
      screen.h
      screen_manager.cpp
      screen_manager.h
      gesture_manager.cpp
      gesture_manager.h
      clock_screen.cpp
      clock_screen.h
      music_screen.cpp
      music_screen.h

    ui/
      fonts/
        music_fonts.cpp
        music_fonts.h
      styles/
        lvgl_style_helpers.cpp
        lvgl_style_helpers.h
      theme/
        colors.h

  main.cpp
```

Existing low-level hardware components remain outside `main/app`:

- `components/i2c_bsp`
- `components/i2c_equipment`
- `components/adc_bsp`
- `components/lcd_bl_pwm_bsp`
- `components/SensorLib`

## Runtime Data Flow

Primary flow:

```text
MQTT / RTC / Power / Network tasks
        |
        v
Services update latest snapshots
        |
        v
Services publish lightweight events
        |
        v
ScreenManager calls active Screen::onTick()
        |
        v
Presenter drains EventBus and reads snapshots
        |
        v
Presenter updates feature model
        |
        v
View renders LVGL widgets
```

The active screen is the only consumer of UI events. When a screen is inactive, its presenter is stopped and must not mutate its view.

## Core Interfaces

### Screen

```cpp
class Screen {
public:
    virtual ~Screen() = default;
    virtual void onEnter() = 0;
    virtual void onExit() = 0;
    virtual void onTick() = 0;
};
```

Rules:

- `onEnter()`, `onExit()`, and `onTick()` are called with the LVGL lock held.
- `onTick()` is the only screen-level place where presenter event consumption happens.
- Screens own their presenter and view instances.

### Presenter

```cpp
class Presenter {
public:
    virtual ~Presenter() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void tick() = 0;
};
```

Rules:

- `start()` reads service snapshots and renders initial state.
- `tick()` drains events relevant to the feature, refreshes snapshots when needed, and calls view render methods.
- `stop()` clears transient state and prevents later rendering into a destroyed view.
- `tick()` drains all currently available UI EventBus events in one call.
- For each unique snapshot type with a new revision, `tick()` reads the corresponding service snapshot exactly once, regardless of how many events of that type were queued.
- `tick()` may discard events for inactive or unrelated features because services retain authoritative snapshots for later `start()` calls.

### EventBus

```cpp
class EventBus {
public:
    static EventBus& get();
    bool publish(const AppEvent& event);
    bool poll(AppEvent* event);
};
```

Implementation rules:

- `EventBus` is a process-wide singleton accessed through `EventBus::get()`.
- Services and presenters use the singleton rather than constructing their own buses.
- Host unit tests may replace or reset the singleton through a test-only hook, and may test `EventQueue` directly without the singleton.
- Fixed-size ring buffer.
- No dynamic allocation in `publish()`.
- `event_queue.h` contains the testable ring-buffer implementation.
- Queue overflow is counted and logged, not fatal.
- Events are small and copyable.
- Multi-producer, single-consumer model: service tasks may publish, but only the UI tick polls.
- Polling drains the queue for the active screen; irrelevant events are discarded because services retain authoritative snapshots.
- `poll()` is non-blocking.
- `poll()` returns `true` and writes one event when the queue is non-empty.
- `poll()` returns `false` when the queue is empty.

## Event Types

Initial event set:

```cpp
enum class AppEventType {
    ClockTimeChanged,
    PowerStateChanged,
    NetworkStateChanged,
    MusicStateChanged,
    CoverStateChanged,
    FeatureAction,
};

struct FeatureActionEvent {
    uint8_t screen_id;
    uint8_t action_id;
};
```

Navigation changes do not need EventBus events for screen switching. A passive `ScreenChanged` event can be added later for analytics or settings, but it is not part of the first refactor.

`FeatureAction` is used only for feature-local user intents, such as a music play/pause button tap or future settings control action.

Rules:

- `GestureManager` or a view callback publishes `FeatureAction`.
- `screen_id` identifies the target screen presenter.
- `action_id` is interpreted by the target feature. For example, Music may define `PlayPause`, `Previous`, and `Next`.
- Only the active screen presenter consumes matching `FeatureAction` events.
- Navigation gestures do not use `FeatureAction`.

## Service Responsibilities

### TimeService

Wraps RTC reads currently performed in `ClockFaceScreen`.

Responsibilities:

- Read `i2c_rtc_get()`.
- Validate RTC date/time.
- Expose latest `ClockSnapshot`.
- Publish `ClockTimeChanged` when relevant display fields change.

### PowerService

Wraps `PowerManager::getState()`.

Responsibilities:

- Expose latest battery percentage, external power, and dimmed state.
- `dimmed` is read from `PowerManager::getState()`, which already aggregates idle state and backlight PWM changes.
- `PowerService` does not call `lcd_bl_pwm_bsp` directly.
- Publish `PowerStateChanged` when the packed power state changes.
- Preserve existing battery display hysteresis behavior through the presenter/model layer.

### NetworkService

Wraps `ClockNet::getStatus()`.

Responsibilities:

- Expose latest WiFi and NTP status.
- Publish `NetworkStateChanged` when WiFi, sync-in-progress, or synced flags change.

### MqttService

Owns MQTT connectivity and raw Shairport Sync ingestion currently handled in `MusicMqtt`.

Responsibilities:

- Connect, reconnect, subscribe, and parse MQTT packets.
- Own Shairport Sync payload parsing that is currently near `music_state`.
- Update latest `MusicState` snapshot.
- Publish `MusicStateChanged`.
- Pass cover payload ownership to `CoverService`.

In the first refactor, `MqttService` does not subscribe to `NetworkStateChanged`. It may continue to use existing connection behavior and retry rules. Explicit service-to-service event consumption is deferred.

### CoverService

Owns music cover image lifecycle.

Responsibilities:

- Accept cover payloads from `MqttService`.
- Decode or transform cover data outside UI tick.
- Own active cover buffers.
- Expose borrowed cover handles to the presenter/view.
- Publish `CoverStateChanged`.

## Feature Responsibilities

### Clock Feature

`clock_model.*`:

- Stores UI-thread display model.
- Handles derived display state such as last rendered time, dimmed flag, battery display threshold, and date labels.

`clock_presenter.*`:

- Reads `TimeService`, `PowerService`, and `NetworkService` snapshots.
- Handles `ClockTimeChanged`, `PowerStateChanged`, and `NetworkStateChanged` events.
- Converts snapshots into view render calls.
- Applies existing display rules:
  - invalid RTC displays `--:--`, `RTC`, and `--/--`
  - colon blinks with seconds
  - battery label changes only when the existing threshold rule allows it
  - dimmed state recolors active clock segments and status elements

`clock_view.*`:

- Owns LVGL objects for the clock screen.
- Builds the same layout and colors as the existing `ClockFaceScreen`.
- Provides methods such as:
  - `renderTime(const ClockTimeViewState&)`
  - `renderBattery(const BatteryViewState&)`
  - `renderNetwork(const NetworkViewState&)`
  - `setDimmed(bool)`

`widgets/seven_segment_widget.*`:

- Encapsulates the current seven-segment digit block creation and segment state updates.
- Does not read services or EventBus.

### Music Feature

`music_state.h`:

- Shared playback and track state data.
- Header-only data definitions. Parsing helpers live in `MqttService` or a private service helper, not in `music_state`.

`music_model.*`:

- UI-thread feature model.
- Computes subtitle text, progress values, elapsed/duration labels, and visualizer state.
- Owns no LVGL objects and no service resources.

`music_presenter.*`:

- Reads `MqttService` music snapshot and `CoverService` state.
- Handles `MusicStateChanged`, `CoverStateChanged`, and feature action events.
- Preserves existing behavior:
  - local progress estimate while playing
  - play/pause icon update
  - title and subtitle long-text behavior
  - visualizer bar heights
  - fallback cover placeholder until real cover is ready

`music_view.*`:

- Owns LVGL objects for the music screen.
- Builds the same layout, colors, fonts, and positions as the existing `MusicPlayerScreen`.
- Does not decode JPEGs or read MQTT.

`widgets/cover_widget.*`:

- Encapsulates cover image object, placeholder band/accent, and cover image source assignment.
- Uses borrowed cover buffers from `CoverService`.

`widgets/visualizer_widget.*`:

- Encapsulates spectrum bar LVGL objects and update operations.

## Screen And Gesture Design

`ScreenManager`:

- Owns `ClockScreen` and `MusicScreen`.
- Starts on Clock.
- Handles left/right screen navigation directly.
- Calls `onEnter()`, `onExit()`, and `onTick()` with LVGL lock held.
- `ScreenManager::tick()` is called from an `lv_timer` or the LVGL task loop, with the LVGL lock held, at the same cadence as `lv_timer_handler()`.

`GestureManager`:

- Owns swipe detection currently implemented in `screen_nav.*`.
- Emits navigation decisions to `ScreenManager` for screen switching.
- Publishes feature gestures or button intents to EventBus when needed.

Initial navigation behavior remains unchanged:

- Left swipe on Clock switches to Music.
- Right swipe on Music switches to Clock.
- Other swipes are ignored.

## Migration Strategy

The migration will be done in small steps while preserving behavior.

1. Add `main/app` structure and move pure navigation tests to the new screen/gesture layer.
2. Add `EventQueue` and `EventBus` with standalone tests.
3. Introduce `Screen`, `ScreenManager`, and `GestureManager` under `main/app/screens`.
4. Wrap existing Clock data reads in `TimeService`, `PowerService`, and `NetworkService`.
5. Split `ClockFaceScreen` into `ClockScreen`, `ClockPresenter`, `ClockModel`, `ClockView`, and `SevenSegmentWidget`.
6. Create `MqttService`: move Shairport Sync payload parsing from old music state helpers, wrap MQTT state access, expose `MusicState` snapshots, and make `features/music/music_state.h` a header-only data definition file.
7. Move cover ownership and decode/update flow into `CoverService`.
8. Split `MusicPlayerScreen` into `MusicScreen`, `MusicPresenter`, `MusicModel`, `MusicView`, `CoverWidget`, and `VisualizerWidget`.
9. Remove old top-level screen files once the new screens build and behave the same.

## Testing And Verification

Pure logic tests:

- Event queue publish/poll order.
- Event queue overflow behavior.
- Gesture detection and screen navigation.
- MQTT music payload parsing.
- Music model progress/time formatting.
- Clock model invalid RTC and dimming behavior.

Firmware verification:

- `cmake --build build`
- Flash to device if hardware is available.
- Confirm boot still starts on Clock.
- Confirm left swipe switches to Music.
- Confirm right swipe switches to Clock.
- Confirm clock time, date, WiFi, NTP, power, battery, dimming behavior remain unchanged.
- Confirm music title, subtitle, play/pause icon, progress, visualizer, cover placeholder, and real cover behavior remain unchanged.

Simulator verification:

- Existing SDL music UI simulator continues to render the music screen.
- The simulator can instantiate `MusicScreen` or a music feature harness instead of the old `MusicPlayerScreen`.

## Acceptance Criteria

- The project builds with the new `main/app` MVP + EventBus structure.
- Clock and Music visual output remain equivalent to the current implementation.
- No service task directly calls presenter or view methods.
- Presenters consume EventBus events only through `Screen::onTick()`.
- Views do not subscribe to EventBus or read services.
- Service snapshots are readable on presenter `start()`.
- EventBus events remain lightweight and copyable.
- Cover buffers are owned by `CoverService`; views only borrow active buffers.
- Existing navigation behavior is preserved.
