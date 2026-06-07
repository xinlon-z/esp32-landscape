#include "music_presenter.h"

#include "app/core/event/event_bus.h"
#include "app/services/cover_service.h"
#include "app/services/mqtt_service.h"
#include "app/services/power_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

namespace {
constexpr uint32_t kFramesPerSecond = 44100;

uint32_t totalFrames(const MusicState& state)
{
    if (state.progress_end_frame <= state.progress_start_frame) {
        return 0;
    }
    return state.progress_end_frame - state.progress_start_frame;
}
} // namespace

MusicPresenter::MusicPresenter(MusicView& view) : view_(view) {}

void MusicPresenter::start()
{
    running_ = true;

    music_state_ = MqttService::get().snapshot();
    last_music_revision_ = music_state_.revision;
    const CoverState cover = CoverService::get().active();
    last_cover_id_ = cover.cover_id;
    last_cover_status_ = cover.status;

    PowerService::get().poll();
    syncDimState();
    renderMusic();
    renderCover();
}

void MusicPresenter::stop()
{
    running_ = false;
}

void MusicPresenter::tick()
{
    if (!running_) {
        return;
    }

    PowerService::get().poll();

    bool music_changed = false;
    bool cover_changed = false;
    bool power_changed = false;
    AppEvent event{};
    while (EventBus::get().poll(&event)) {
        if (event.type == AppEventType::MusicStateChanged &&
            event.payload.music_state.revision != last_music_revision_) {
            last_music_revision_ = event.payload.music_state.revision;
            music_changed = true;
        } else if (event.type == AppEventType::CoverStateChanged &&
                   (event.payload.cover_state.cover_id != last_cover_id_ ||
                    event.payload.cover_state.status != last_cover_status_)) {
            last_cover_id_ = event.payload.cover_state.cover_id;
            last_cover_status_ = event.payload.cover_state.status;
            cover_changed = true;
        } else if (event.type == AppEventType::PowerStateChanged) {
            power_changed = true;
        } else if (event.type == AppEventType::FeatureAction &&
                   event.payload.feature_action.screen_id == static_cast<uint8_t>(ScreenId::Music)) {
            music_changed = true;
        }
    }

    if (music_changed) {
        music_state_ = MqttService::get().snapshot();
        last_music_revision_ = music_state_.revision;
        ESP_LOGI("music_pre", "[trace] music rev=%u rendered (title=%s)",
                 music_state_.revision, music_state_.title);
    }
    if (power_changed) {
        syncDimState();
    }

    renderMusic();
    if (cover_changed) {
        renderCover();
    }
}

void MusicPresenter::syncDimState()
{
    const bool now_dim = PowerService::get().snapshot().dimmed;
    if (now_dim != dimmed_) {
        dimmed_ = now_dim;
        view_.setDimmed(dimmed_);
    }
}

void MusicPresenter::renderMusic()
{
    view_.render(model_.build(music_state_, elapsedFramesForUi(music_state_)));
}

void MusicPresenter::renderCover()
{
    const CoverState cover = CoverService::get().active();
    last_cover_id_ = cover.cover_id;
    last_cover_status_ = cover.status;

    if (cover.cover_id == 0) {
        // No cover at all — show placeholder.
        view_.renderCoverPlaceholder();
        return;
    }
    if (cover.status != CoverStatus::Ready || !cover.has_pixels) {
        // Cover is loading or errored. Keep the previous cover visible rather
        // than blanking to a placeholder — the display looks better with a
        // stale cover than with nothing while the new JPEG decodes.
        return;
    }

    BorrowedCover borrowed{};
    if (CoverService::get().borrow(cover.cover_id, &borrowed) && borrowed.image) {
        ESP_LOGI("music_pre", "[trace] cover %u render status=%d",
                 last_cover_id_, static_cast<int>(last_cover_status_));
        view_.renderCover(borrowed);
    } else {
        view_.renderCoverPlaceholder();
    }
}

uint32_t MusicPresenter::elapsedFramesForUi(const MusicState& state) const
{
    const uint32_t total = totalFrames(state);
    if (total == 0) {
        return 0;
    }

    uint32_t elapsed = 0;
    if (state.progress_current_frame > state.progress_start_frame) {
        elapsed = state.progress_current_frame - state.progress_start_frame;
    }
    if (state.playing && state.last_progress_ms != 0) {
        const uint32_t ms = lv_tick_elaps(state.last_progress_ms);
        const uint32_t whole_s = ms / 1000u;
        const uint32_t frac_ms = ms - whole_s * 1000u;
        elapsed += whole_s * kFramesPerSecond + (frac_ms * kFramesPerSecond) / 1000u;
    }
    return elapsed > total ? total : elapsed;
}
