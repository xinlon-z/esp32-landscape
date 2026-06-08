# Gesture Navigation Feedback Design

## Goal

Prevent accidental music-to-clock navigation caused by slow ghost touch drift, while making intentional left/right screen navigation feel more like Android full-screen gestures.

The observed false switch was:

```text
switch music -> clock by swipe right dx=143 dy=1 dt=665 samples=5
```

That input satisfies the existing distance-only rule, but it is too slow and weak to be treated as an intentional screen navigation gesture.

## Interaction Model

Screen navigation remains:

- Clock -> Music: left swipe
- Music -> Clock: right swipe

Gesture recognition uses LVGL's built-in pointer gesture event plus a local guard:

- `LV_EVENT_GESTURE` provides the mature direction trigger used by LVGL itself.
- Edge-start gestures are more permissive and feel natural.
- Center-start gestures remain slightly stricter.
- Slow horizontal drift is rejected even if it crosses the minimum distance threshold.

The screen width is 640 px. Edge-start means the initial touch begins near the navigation edge:

- Clock -> Music left swipe: starts near the right edge.
- Music -> Clock right swipe: starts near the left edge.

## Recognition Rules

Use touch-slop style horizontal and vertical gates:

- Edge-start minimum horizontal movement: 64 px
- Center-start minimum horizontal movement: 80 px
- Maximum final vertical offset: 54 px
- Maximum vertical travel during the gesture: 54 px

Timing and speed gates:

- Edge-start window: 96 px from the relevant screen edge.
- Maximum gesture duration: 900 ms.
- Edge-start minimum horizontal speed: 120 px/s.
- Center-start minimum horizontal speed: 140 px/s.
- Slow drift guard: gestures longer than 600 ms must move at least 160 px.

The known false gesture (`143 px / 665 ms`) must be rejected. Normal deliberate swipes around `64-80 px / 500 ms` should be accepted depending on edge/center start.

## Visual Feedback

Use the edge pill cue direction after device feedback showed the Hybrid animation was too choppy:

- Show an edge pill/arrow cue while dragging in a valid navigation direction.
- On release:
  - If the gesture qualifies, clear the cue and switch screens immediately.
  - If it does not qualify, clear the cue and do not switch.

Implementation must stay lightweight for LVGL:

- No full-screen page sliding animation.
- No active screen root translation.
- No large off-screen buffers.
- Use small overlay objects and direct opacity/position updates while dragging.
- Keep redraw cost bounded on the 640 x 172 display.

## Architecture

`SwipeGestureDetector` owns gesture classification:

- Track start point, current point, min/max Y, duration, sample count.
- Expose stats needed by `ScreenManager`.
- Return `None` for slow drift and ambiguous movement.

`ScreenManager` owns UI feedback:

- On `LV_EVENT_PRESSED`, start tracking and reset the gesture overlay.
- On `LV_EVENT_PRESSING`, update gesture progress and overlay state.
- On `LV_EVENT_GESTURE`, use LVGL's direction and local classification to switch as soon as the gesture is valid.
- On `LV_EVENT_RELEASED`, keep the local detector as a fallback.
- If navigation succeeds, clear the cue and switch screens.
- If navigation fails, clear the cue.

The overlay should be private to the screen navigation layer. Clock and Music views should not need gesture-specific code.

## Testing

Host tests cover the decision logic:

- Slow drift matching the production log is rejected.
- Fast edge swipe is accepted.
- Fast center swipe is accepted only when deliberate enough.
- Vertical drag remains rejected.
- Existing left/right screen mapping remains unchanged.

Firmware build verifies LVGL integration.
