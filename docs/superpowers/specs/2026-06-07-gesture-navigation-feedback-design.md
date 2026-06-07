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

Gesture recognition uses a hybrid rule:

- Edge-start gestures are more permissive and feel natural.
- Center-start gestures are allowed, but must be faster and more deliberate.
- Slow horizontal drift is rejected even if it crosses the minimum distance threshold.

The screen width is 640 px. Edge-start means the initial touch begins near the navigation edge:

- Clock -> Music left swipe: starts near the right edge.
- Music -> Clock right swipe: starts near the left edge.

## Recognition Rules

Keep the current horizontal and vertical gates:

- Minimum horizontal movement: 120 px
- Maximum final vertical offset: 54 px
- Maximum vertical travel during the gesture: 54 px

Add timing gates:

- Edge-start window: 96 px from the relevant screen edge.
- Edge-start gestures: maximum duration 520 ms, minimum horizontal speed 650 px/s.
- Center-start gestures: maximum duration 360 ms, minimum horizontal speed 1100 px/s.

The known false gesture (`143 px / 665 ms`) must be rejected. A real quick swipe like (`427 px / 95 ms`) must remain accepted.

## Visual Feedback

Use the Hybrid direction chosen during brainstorming:

- Show an edge pill/arrow cue while dragging in a valid navigation direction.
- Nudge the current content slightly in the drag direction.
- On release:
  - If the gesture qualifies, play a completion cue no longer than 140 ms and switch screens.
  - If it does not qualify, return the cue/content to rest within 140 ms and do not switch.

Implementation must stay lightweight for LVGL:

- No full-screen page sliding animation.
- No large off-screen buffers.
- Use small overlay objects and short LVGL animations.
- Keep redraw cost bounded on the 640 x 172 display.

## Architecture

`SwipeGestureDetector` owns gesture classification:

- Track start point, current point, min/max Y, duration, sample count.
- Expose stats needed by `ScreenManager`.
- Return `None` for slow drift and ambiguous movement.

`ScreenManager` owns UI feedback:

- On `LV_EVENT_PRESSED`, start tracking and reset the gesture overlay.
- On `LV_EVENT_PRESSING`, update gesture progress and overlay state.
- On `LV_EVENT_RELEASED`, ask the detector for classification.
- If navigation succeeds, play a short completion cue and switch screens.
- If navigation fails, animate the cue back to rest.

The overlay should be private to the screen navigation layer. Clock and Music views should not need gesture-specific code.

## Testing

Host tests cover the decision logic:

- Slow drift matching the production log is rejected.
- Fast edge swipe is accepted.
- Fast center swipe is accepted only when deliberate enough.
- Vertical drag remains rejected.
- Existing left/right screen mapping remains unchanged.

Firmware build verifies LVGL integration.
