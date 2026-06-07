#pragma once

#include <stdint.h>

struct NetworkSnapshot {
    bool wifi_connected = false;
    bool sync_in_progress = false;
    bool ntp_synced = false;
    uint32_t revision = 0;
};

class NetworkService {
public:
    static NetworkService& get();
    NetworkSnapshot snapshot();
    void poll();
};
