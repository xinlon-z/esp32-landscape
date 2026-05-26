#pragma once

#include "music_model.h"
#include "music_view.h"

#include <stdint.h>

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
    MusicState music_state_{};
    uint32_t last_music_revision_ = 0;
    uint32_t last_cover_id_ = 0;
    CoverStatus last_cover_status_ = CoverStatus::Idle;
    bool dimmed_ = false;

    void renderMusic();
    void renderCover();
    void syncDimState();
    uint32_t elapsedFramesForUi(const MusicState& state) const;
};
