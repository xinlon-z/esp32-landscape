#ifndef CLOCK_NET_H
#define CLOCK_NET_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool wifi_connected;
    bool ntp_synced;
    bool sync_in_progress;
} clock_net_status_t;

void clock_net_init(void);
clock_net_status_t clock_net_get_status(void);

#ifdef __cplusplus
}
#endif

#endif
