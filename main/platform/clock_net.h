#pragma once

namespace ClockNet {

struct Status {
    bool wifi_connected   = false;
    bool ntp_synced       = false;
    bool sync_in_progress = false;
};

void   init();
void   pauseForSleep();
void   requestSync();
Status getStatus();

} // namespace ClockNet
