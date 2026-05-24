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
