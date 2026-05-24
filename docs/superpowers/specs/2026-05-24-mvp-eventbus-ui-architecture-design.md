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
struct MusicStateChangedEvent {
    uint32_t revision;
};

struct CoverStateChangedEvent {
    uint32_t cover_id;
    CoverStatus status;
};
```

### ADR 2: Services Own Snapshots

Decision: every service that adapts external state owns the latest snapshot.

Examples:

- `TimeService` owns the latest clock snapshot.
- `PowerService` owns the latest power snapshot.
- `NetworkService` owns the latest WiFi/NTP snapshot.
- `MqttService` owns the latest MQTT connection and raw message state.
- `CoverService` owns cover loading state and cover buffers.

Presenters read snapshots on `start()` and then consume lightweight events on `tick()`.

Snapshot access rule:

- Services expose snapshot copy APIs, not mutable references.
- Snapshot reads are protected by the service's mutex, atomic packed state, or equivalent thread-safe mechanism.
- Presenters receive a copy and never keep pointers into service-owned snapshot storage.

Reason:

- Avoids sticky-event complexity in the EventBus.
- Makes screen re-entry deterministic.
- Allows EventBus queue depth to stay small, around 8 to 16 events.

### ADR 3: Presenter Consumes Events Only In UI Tick

Decision: presenters must consume EventBus events only from the UI tick while the LVGL lock is held.

This is a mandatory thread-safety rule. MQTT, power, network, and cover tasks must never call presenter or view methods directly.

Reason:

- LVGL updates must stay on the UI side of the system.
- Keeps cross-task communication limited to services and the EventBus queue.
- Prevents callbacks from updating deleted or inactive screen widgets.

### ADR 4: View Never Subscribes To EventBus

Decision: views do not subscribe to `EventBus`, call services, or know about external event flow.

Views only expose render methods used by presenters.

Reason:

- Keeps LVGL widget code local and testable through presenter output.
- Prevents global events from leaking into visual layout code.
- Makes it safe to redesign a view without changing service code.

### ADR 5: `music_state` And `music_model` Have Separate Responsibilities

Decision: `music_state.*` defines shared plain data types. `music_model.*` defines UI-thread view-model logic for the music feature.

`music_state.*`:

- Plain data structures and enums.
- Trivially copyable where practical.
- Shared by services, events, and presenters.
- Does not include `music_model.h`.

`music_model.*`:

- Presenter-owned feature model.
- Formats and derives presentation state such as subtitle text, progress value, time labels, and visualizer render values.
- Can include `music_state.h`.
- Is not included by services.

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

### ADR 8: Services Do Not Subscribe To EventBus In The First Refactor

Decision: for this refactor, EventBus is a service-to-UI notification path only.

Service coordination uses direct service-owned tasks, existing system state, and explicit service APIs. Services publish events but do not consume EventBus events.

Reason:

- Keeps EventBus consumption on one thread path: `Screen::onTick()`.
- Avoids introducing a second service-side event loop and its locking rules.
- Reduces migration risk while the current firmware already has working task ownership in `ClockNet`, `PowerManager`, and `MusicMqtt`.

Future option:

- A separate service event loop can be introduced later if service-to-service decoupling becomes necessary.
- That would require its own queue, threading contract, and tests. It must not reuse the UI EventBus consumption path implicitly.

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
        music_state.cpp
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
      screen.cpp
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

### EventBus

```cpp
class EventBus {
public:
    bool publish(const AppEvent& event);
    bool poll(AppEvent* event);
};
```

Implementation rules:

- Fixed-size ring buffer.
- No dynamic allocation in `publish()`.
- `event_queue.h` contains the testable ring-buffer implementation.
- Queue overflow is counted and logged, not fatal.
- Events are small and copyable.
- Multi-producer, single-consumer model: service tasks may publish, but only the UI tick polls.
- Polling drains the queue for the active screen; irrelevant events are discarded because services retain authoritative snapshots.

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
```

Navigation changes do not need EventBus events for screen switching. A passive `ScreenChanged` event can be added later for analytics or settings, but it is not part of the first refactor.

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

`music_state.*`:

- Shared playback and track state data.
- Includes parsing helpers only if they are service/domain-level and independent of LVGL.

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
6. Move music shared state to `features/music/music_state.*`.
7. Wrap MQTT state access in `MqttService`.
8. Move cover ownership and decode/update flow into `CoverService`.
9. Split `MusicPlayerScreen` into `MusicScreen`, `MusicPresenter`, `MusicModel`, `MusicView`, `CoverWidget`, and `VisualizerWidget`.
10. Remove old top-level screen files once the new screens build and behave the same.

## Testing And Verification

Pure logic tests:

- Event queue publish/poll order.
- Event queue overflow behavior.
- Gesture detection and screen navigation.
- Music state parsing.
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
