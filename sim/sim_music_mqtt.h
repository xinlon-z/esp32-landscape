#pragma once

#include <stdint.h>

namespace SimMusicMqtt {

struct Config {
    const char* host = "192.168.31.100";
    uint16_t port = 1883;
    const char* topic = "shairport/livingroom";
    const char* username = "mqtt";
    const char* password = "";
    const char* screenshot_path = nullptr;
    int run_ms = 0;
    int recreate_at_ms = 0;
    bool offline = false;
};

void configure(const Config& config);
Config parseArgs(int argc, char** argv);
void printUsage(const char* argv0);

} // namespace SimMusicMqtt
