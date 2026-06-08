# Gesture Navigation Feedback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reject slow ghost-touch drift while adding lightweight Android-style edge pill swipe feedback for screen navigation.

**Architecture:** LVGL's built-in `LV_EVENT_GESTURE` provides the mature direction trigger. `SwipeGestureDetector` remains the local guard for stats, edge/center thresholds, and slow-drift rejection. `ScreenManager` owns a small LVGL overlay that follows drag progress without moving Clock or Music view roots.

**Tech Stack:** ESP-IDF v6.0.1, LVGL 8.4, C++17 host tests with GoogleTest.

---

## File Structure

- Modify `main/app/screens/gesture_manager.h`: add edge-start parameters and progress helpers to the gesture API.
- Modify `main/app/screens/gesture_manager.cpp`: implement edge/center touch-slop gates, slow-drift filtering, live classification, and progress calculation.
- Modify `tests/test_screen_nav.cpp`: add red/green coverage for the production false gesture, fast edge swipe, stricter center swipe, and progress helper.
- Modify `main/app/screens/screen_manager.h`: add overlay object pointers and gesture feedback helper methods.
- Modify `main/app/screens/screen_manager.cpp`: create/update/reset the edge pill overlay during drag and handle `LV_EVENT_GESTURE`.
- Modify `tests/stubs/lvgl.h`: add only the LVGL stubs needed by `ScreenManager` if host tests require them.

---

### Task 1: Gesture Classification Rules

**Files:**
- Modify: `main/app/screens/gesture_manager.h`
- Modify: `main/app/screens/gesture_manager.cpp`
- Test: `tests/test_screen_nav.cpp`

- [ ] **Step 1: Write failing tests**

Add cases to `tests/test_screen_nav.cpp`:

```cpp
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
    EXPECT_EQ(detector.release({380, 92}, 3400, &stats), SwipeDirection::None)
        << "center gestures need stricter timing than edge gestures";

    detector.press({250, 90}, 4000);
    EXPECT_EQ(detector.release({480, 92}, 4160, &stats), SwipeDirection::Right)
        << "fast deliberate center swipe should still work";
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build-tests --target test_screen_nav
./build-tests/test_screen_nav --gtest_filter=ScreenNav.SwipeTimingAndEdgeRules
```

Expected: fails because the current detector accepts the slow drift.

- [ ] **Step 3: Implement minimal classification**

Update `gesture_manager.h`:

```cpp
struct SwipeGestureStats {
    int dx;
    int dy;
    uint32_t duration_ms;
    uint16_t samples;
    bool edge_start;
};
```

Update `gesture_manager.cpp` constants and `release()`:

```cpp
constexpr int kEdgeMinSwipeX = 64;
constexpr int kCenterMinSwipeX = 80;
constexpr int kScreenW = 640;
constexpr int kEdgeStartPx = 96;
constexpr uint32_t kMaxDurationMs = 900;
constexpr uint32_t kEdgeMinSpeedPxPerSec = 120;
constexpr uint32_t kCenterMinSpeedPxPerSec = 140;
constexpr uint32_t kSlowDriftDurationMs = 600;
constexpr int kSlowDriftDistancePx = 160;

bool edgeStartForDirection(TouchPoint start, int dx)
{
    if (dx < 0) {
        return start.x >= kScreenW - kEdgeStartPx;
    }
    if (dx > 0) {
        return start.x <= kEdgeStartPx;
    }
    return false;
}
```

Then reject gestures that fail the edge/center distance gates, exceed max duration, fall below the corresponding min speed, or match the slow-small drift guard. Treat `duration == 0` as a fast deliberate gesture for test compatibility.

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
cmake --build build-tests --target test_screen_nav
./build-tests/test_screen_nav --gtest_filter=ScreenNav.SwipeTimingAndEdgeRules
```

Expected: pass.

---

### Task 2: Gesture Progress API

**Files:**
- Modify: `main/app/screens/gesture_manager.h`
- Modify: `main/app/screens/gesture_manager.cpp`
- Test: `tests/test_screen_nav.cpp`

- [ ] **Step 1: Write failing tests**

Add to `tests/test_screen_nav.cpp`:

```cpp
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
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build-tests --target test_screen_nav
./build-tests/test_screen_nav --gtest_filter=ScreenNav.DragProgressReportsDirectionAndClamps
```

Expected: compile failure because `SwipeGestureProgress` and `progress()` do not exist.

- [ ] **Step 3: Implement progress helper**

Add to `gesture_manager.h`:

```cpp
struct SwipeGestureProgress {
    SwipeDirection direction = SwipeDirection::None;
    uint16_t per_mille = 0;
    int dx = 0;
    bool edge_start = false;
};

bool progress(SwipeGestureProgress* progress) const;
```

Store the latest point in `SwipeGestureDetector`, update it in `press()` and `move()`, and compute `per_mille = min(abs(dx) * 1000 / kMinSwipeX, 1000)`.

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
cmake --build build-tests --target test_screen_nav
./build-tests/test_screen_nav --gtest_filter=ScreenNav.DragProgressReportsDirectionAndClamps
```

Expected: pass.

---

### Task 3: Edge Pill LVGL Feedback Overlay

**Files:**
- Modify: `main/app/screens/screen_manager.h`
- Modify: `main/app/screens/screen_manager.cpp`

- [ ] **Step 1: Add overlay state and helpers**

Add private members to `ScreenManager`:

```cpp
lv_obj_t* gesture_overlay_ = nullptr;
lv_obj_t* gesture_pill_ = nullptr;
lv_obj_t* gesture_arrow_ = nullptr;
lv_obj_t* gesture_screen_root_ = nullptr;
```

Add helper declarations:

```cpp
void ensureGestureOverlay();
void updateGestureFeedback(const SwipeGestureProgress& progress);
void clearGestureFeedback();
```

- [ ] **Step 2: Implement minimal overlay**

In `screen_manager.cpp`, create a transparent full-screen overlay on `lv_scr_act()`, with:

- one rounded pill object at the active edge
- one label arrow using `LV_SYMBOL_LEFT` or `LV_SYMBOL_RIGHT`
- opacity controlled by drag progress

Do not move `lv_scr_act()` or schedule release animations. Keep feedback to direct overlay opacity/position updates while dragging.

- [ ] **Step 3: Wire to gesture events**

In `onGestureEvent()`:

```cpp
} else if (code == LV_EVENT_PRESSING) {
    manager->swipe_detector_.move(currentTouchPoint());
    SwipeGestureProgress progress{};
    if (manager->swipe_detector_.progress(&progress)) {
        manager->updateGestureFeedback(progress);
    }
} else if (code == LV_EVENT_RELEASED) {
    SwipeGestureStats stats{};
    const SwipeDirection swipe =
        manager->swipe_detector_.release(currentTouchPoint(), tick, &stats);
    manager->clearGestureFeedback();
    manager->handleSwipe(swipe, stats);
}
```

Reset feedback on `PRESS_LOST`, `switchTo()`, and `destroy()`.

- [ ] **Step 4: Build firmware**

Run:

```bash
IDF_PATH=/Users/xinzlong/.espressif/v6.0.1/esp-idf cmake --build build
```

Expected: build completes.

---

### Task 4: Full Verification and Commit

**Files:**
- All modified files from previous tasks

- [ ] **Step 1: Run full host tests**

Run:

```bash
ctest --test-dir build-tests --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 2: Run firmware build**

Run:

```bash
IDF_PATH=/Users/xinzlong/.espressif/v6.0.1/esp-idf cmake --build build
```

Expected: build completes; the existing app partition space warning is acceptable.

- [ ] **Step 3: Check diff hygiene**

Run:

```bash
git diff --check
git status --short
```

Expected: no whitespace warnings; only intended source/test/plan files modified, plus pre-existing untracked `.codegraph/` and `.cursor/`.

- [ ] **Step 4: Commit**

Run:

```bash
git add docs/superpowers/plans/2026-06-07-gesture-navigation-feedback.md \
        main/app/screens/gesture_manager.h \
        main/app/screens/gesture_manager.cpp \
        main/app/screens/screen_manager.h \
        main/app/screens/screen_manager.cpp \
        tests/test_screen_nav.cpp \
        tests/stubs/lvgl.h
git commit -m "Improve gesture navigation feedback"
```
