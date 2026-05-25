#include "../main/app/services/mqtt_service.cpp"
#include "../main/app/services/cover_service.cpp"

#include "esp_heap_caps.h"

#include <stdio.h>

namespace {
uint8_t* pending_cover = nullptr;
uint32_t pending_cover_size = 0;

static int expect(bool condition, const char* message)
{
    if (!condition) {
        printf("%s\n", message);
        return 1;
    }
    return 0;
}
} // namespace

namespace MusicMqtt {
void init()
{
}

bool takeCover(CoverImage* cover)
{
    if (!cover || !pending_cover || pending_cover_size == 0) {
        return false;
    }
    cover->data = pending_cover;
    cover->size = pending_cover_size;
    pending_cover = nullptr;
    pending_cover_size = 0;
    return true;
}
} // namespace MusicMqtt

int main()
{
    int failures = 0;

    CoverService::get().clear();
    EventBus::get().resetForTest();

    pending_cover_size = 4;
    pending_cover = static_cast<uint8_t*>(heap_caps_malloc(pending_cover_size, MALLOC_CAP_8BIT));
    pending_cover[0] = 0xff;
    pending_cover[1] = 0xd8;
    pending_cover[2] = 0xff;
    pending_cover[3] = 0xd9;

    uint8_t* data = reinterpret_cast<uint8_t*>(0x1);
    uint32_t size = 99;
    failures += expect(MqttService::get().takeCover(&data, &size), "takeCover should consume pending cover");
    failures += expect(data == nullptr, "takeCover should transfer ownership to CoverService");
    failures += expect(size == 0, "takeCover should clear caller size after transfer");

    CoverBuffer active = CoverService::get().active();
    failures += expect(active.cover_id == 1, "first cover_id should be 1");
    failures += expect(active.status == CoverStatus::Loading, "cover status should be Loading");
    failures += expect(active.jpeg_data != nullptr, "CoverService should own jpeg bytes");
    failures += expect(active.jpeg_size == 4, "CoverService jpeg size mismatch");

    AppEvent event{};
    failures += expect(EventBus::get().poll(&event), "CoverService should publish CoverStateChanged");
    failures += expect(event.type == AppEventType::CoverStateChanged, "cover event type mismatch");
    failures += expect(event.payload.cover_state.cover_id == active.cover_id, "cover event id mismatch");
    failures += expect(event.payload.cover_state.status == CoverStatus::Loading, "cover event status mismatch");

    pending_cover_size = 4;
    pending_cover = static_cast<uint8_t*>(heap_caps_malloc(pending_cover_size, MALLOC_CAP_8BIT));
    pending_cover[0] = 0xff;
    pending_cover[1] = 0xd8;
    pending_cover[2] = 0xff;
    pending_cover[3] = 0xd9;

    failures += expect(MqttService::get().takeCover(&data, &size), "second takeCover should consume pending cover");
    active = CoverService::get().active();
    failures += expect(active.cover_id == 2, "second cover_id should increment");

    CoverService::get().clear();
    return failures == 0 ? 0 : 1;
}
