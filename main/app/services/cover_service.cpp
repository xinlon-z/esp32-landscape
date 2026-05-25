#include "cover_service.h"

#include "../core/event/event_bus.h"
#include "esp_heap_caps.h"

CoverService& CoverService::get()
{
    static CoverService service;
    return service;
}

uint32_t CoverService::acceptJpeg(uint8_t* data, uint32_t size)
{
    if (!data || size == 0) {
        return 0;
    }

    uint32_t cover_id = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        releaseActive();
        cover_id = ++next_cover_id_;
        active_.cover_id = cover_id;
        active_.status = CoverStatus::Loading;
        active_.jpeg_data = data;
        active_.jpeg_size = size;
    }

    publishChanged(cover_id, CoverStatus::Loading);
    return cover_id;
}

CoverState CoverService::active()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stateFromEntry(active_);
}

bool CoverService::borrow(uint32_t cover_id, BorrowedCover* cover)
{
    if (!cover) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (cover_id == 0 || active_.cover_id != cover_id) {
        *cover = BorrowedCover{};
        return false;
    }

    cover->cover_id = active_.cover_id;
    cover->status = active_.status;
    cover->jpeg_data = active_.jpeg_data;
    cover->jpeg_size = active_.jpeg_size;
    cover->image = active_.pixels ? &active_.image : nullptr;
    cover->pixels = active_.pixels;
    return true;
}

void CoverService::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    releaseActive();
    active_ = CoverEntry{};
}

CoverState CoverService::stateFromEntry(const CoverEntry& entry)
{
    CoverState state{};
    state.cover_id = entry.cover_id;
    state.status = entry.status;
    state.jpeg_size = entry.jpeg_size;
    state.has_jpeg = entry.jpeg_data != nullptr;
    state.has_pixels = entry.pixels != nullptr;
    return state;
}

void CoverService::releaseActive()
{
    if (active_.jpeg_data) {
        heap_caps_free(active_.jpeg_data);
    }
    if (active_.pixels) {
        heap_caps_free(active_.pixels);
    }
    active_ = CoverEntry{};
}

void CoverService::publishChanged(uint32_t cover_id, CoverStatus status)
{
    AppEvent event{};
    event.type = AppEventType::CoverStateChanged;
    event.payload.cover_state.cover_id = cover_id;
    event.payload.cover_state.status = status;
    EventBus::get().publish(event);
}
