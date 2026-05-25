#include "sim_music_mqtt.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "esp_heap_caps.h"
#include "lvgl.h"
#include "app/services/mqtt_service.h"

namespace MusicMqtt {
struct CoverImage {
    uint8_t* data = nullptr;
    uint32_t size = 0;
};

void init();
bool getState(MusicState* state);
bool takeCover(CoverImage* cover);
bool copyLastCover(CoverImage* cover);
} // namespace MusicMqtt

namespace {

constexpr int kSocketTimeoutSeconds = 3;
constexpr int kReconnectDelayMs = 3000;
constexpr int kPayloadLimit = 512;
constexpr int kMaxCoverBytes = 512 * 1024;

std::mutex s_state_mutex;
std::mutex s_cover_mutex;
std::atomic<bool> s_started{false};
SimMusicMqtt::Config s_config;
std::string s_topic_prefix = "shairport/livingroom/";
std::string s_subscribe_topic = "shairport/livingroom/#";
MusicState s_state;
bool s_has_state = false;
uint8_t* s_pending_cover_data = nullptr;
uint32_t s_pending_cover_size = 0;
uint8_t* s_last_cover_data = nullptr;
uint32_t s_last_cover_size = 0;

bool writeAll(int sock, const uint8_t* data, size_t len)
{
    while (len > 0) {
        const ssize_t sent = send(sock, data, len, 0);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        len -= static_cast<size_t>(sent);
    }
    return true;
}

bool readExact(int sock, uint8_t* data, size_t len)
{
    while (len > 0) {
        const ssize_t got = recv(sock, data, len, 0);
        if (got <= 0) {
            return false;
        }
        data += got;
        len -= static_cast<size_t>(got);
    }
    return true;
}

int readByte(int sock)
{
    uint8_t byte = 0;
    return recv(sock, &byte, 1, 0) == 1 ? byte : -1;
}

size_t appendString(std::vector<uint8_t>& packet, const std::string& text)
{
    packet.push_back(static_cast<uint8_t>((text.size() >> 8) & 0xff));
    packet.push_back(static_cast<uint8_t>(text.size() & 0xff));
    packet.insert(packet.end(), text.begin(), text.end());
    return packet.size();
}

void appendRemainingLength(std::vector<uint8_t>& packet, size_t len)
{
    do {
        uint8_t byte = len % 128;
        len /= 128;
        if (len > 0) {
            byte |= 0x80;
        }
        packet.push_back(byte);
    } while (len > 0);
}

bool readRemainingLength(int sock, int* remaining)
{
    int multiplier = 1;
    int value = 0;
    for (int i = 0; i < 4; ++i) {
        const int encoded = readByte(sock);
        if (encoded < 0) {
            return false;
        }
        value += (encoded & 127) * multiplier;
        if ((encoded & 128) == 0) {
            *remaining = value;
            return true;
        }
        multiplier *= 128;
    }
    return false;
}

bool drainBytes(int sock, int len)
{
    uint8_t buffer[256];
    while (len > 0) {
        const int chunk = len > static_cast<int>(sizeof(buffer)) ? static_cast<int>(sizeof(buffer)) : len;
        if (!readExact(sock, buffer, chunk)) {
            return false;
        }
        len -= chunk;
    }
    return true;
}

bool sendConnect(int sock)
{
    std::vector<uint8_t> body;
    appendString(body, "MQTT");
    body.push_back(4);

    uint8_t flags = 0x02;
    const bool has_user = s_config.username && s_config.username[0];
    const bool has_pass = s_config.password && s_config.password[0];
    if (has_user) {
        flags |= 0x80;
    }
    if (has_pass) {
        flags |= 0x40;
    }
    body.push_back(flags);
    body.push_back(0);
    body.push_back(30);
    appendString(body, "host-music-ui-sim");
    if (has_user) {
        appendString(body, s_config.username);
    }
    if (has_pass) {
        appendString(body, s_config.password);
    }

    std::vector<uint8_t> packet;
    packet.push_back(0x10);
    appendRemainingLength(packet, body.size());
    packet.insert(packet.end(), body.begin(), body.end());
    return writeAll(sock, packet.data(), packet.size());
}

bool readConnack(int sock)
{
    uint8_t response[4];
    if (!readExact(sock, response, sizeof(response))) {
        return false;
    }
    if (response[0] != 0x20 || response[1] != 0x02 || response[3] != 0x00) {
        std::cerr << "MQTT CONNACK failed, rc=" << static_cast<int>(response[3]) << "\n";
        return false;
    }
    return true;
}

bool sendSubscribe(int sock)
{
    std::vector<uint8_t> body;
    body.push_back(0);
    body.push_back(1);
    appendString(body, s_subscribe_topic);
    body.push_back(0);

    std::vector<uint8_t> packet;
    packet.push_back(0x82);
    appendRemainingLength(packet, body.size());
    packet.insert(packet.end(), body.begin(), body.end());
    return writeAll(sock, packet.data(), packet.size());
}

int connectSocket()
{
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port[8];
    snprintf(port, sizeof(port), "%u", s_config.port);
    addrinfo* results = nullptr;
    if (getaddrinfo(s_config.host, port, &hints, &results) != 0 || !results) {
        return -1;
    }

    int sock = -1;
    for (addrinfo* addr = results; addr; addr = addr->ai_next) {
        sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (sock < 0) {
            continue;
        }
        timeval timeout = {};
        timeout.tv_sec = kSocketTimeoutSeconds;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        if (connect(sock, addr->ai_addr, addr->ai_addrlen) == 0) {
            break;
        }
        close(sock);
        sock = -1;
    }
    freeaddrinfo(results);
    return sock;
}

const char* fieldFromTopic(const char* topic)
{
    if (!topic || strncmp(topic, s_topic_prefix.c_str(), s_topic_prefix.size()) != 0) {
        return nullptr;
    }
    return topic + s_topic_prefix.size();
}

void updateState(const char* field, const char* payload, size_t payload_len)
{
    if (!field || strcmp(field, "cover") == 0) {
        return;
    }

    const uint32_t progress_ms = strcmp(field, "ssnc/prgr") == 0 ? lv_tick_get() : 0;
    MqttService::get().applyField(field, payload, payload_len, progress_ms);
    {
        std::lock_guard<std::mutex> lock(s_state_mutex);
        s_state = MqttService::get().snapshot();
        s_has_state = true;
    }

    if (strcmp(field, "title") == 0 || strcmp(field, "artist") == 0 ||
        strcmp(field, "album") == 0 || strcmp(field, "active") == 0 ||
        strcmp(field, "playing") == 0 || strcmp(field, "ssnc/prgr") == 0) {
        std::cerr << "mqtt " << field << "=" << std::string(payload, payload_len) << "\n";
    }
}

void updateCover(uint8_t* data, uint32_t size)
{
    if (!data || size == 0) {
        return;
    }
    if (size < 128 || data[0] != 0xff || data[1] != 0xd8) {
        std::cerr << "mqtt cover ignored=" << size << " bytes\n";
        heap_caps_free(data);
        return;
    }
    uint8_t* last_copy = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_8BIT));
    if (last_copy) {
        memcpy(last_copy, data, size);
    }
    {
        std::lock_guard<std::mutex> lock(s_cover_mutex);
        if (s_pending_cover_data) {
            heap_caps_free(s_pending_cover_data);
        }
        s_pending_cover_data = data;
        s_pending_cover_size = size;
        if (last_copy) {
            if (s_last_cover_data) {
                heap_caps_free(s_last_cover_data);
            }
            s_last_cover_data = last_copy;
            s_last_cover_size = size;
        }
    }
    std::cerr << "mqtt cover=" << size << " bytes\n";
}

bool handlePublish(int sock, uint8_t fixed_header, int remaining)
{
    uint8_t len_buf[2];
    if (remaining < 2 || !readExact(sock, len_buf, sizeof(len_buf))) {
        return false;
    }
    remaining -= 2;

    const int topic_len = (static_cast<int>(len_buf[0]) << 8) | len_buf[1];
    if (topic_len <= 0 || topic_len > 240 || remaining < topic_len) {
        return drainBytes(sock, remaining);
    }

    char topic[241];
    if (!readExact(sock, reinterpret_cast<uint8_t*>(topic), topic_len)) {
        return false;
    }
    topic[topic_len] = '\0';
    remaining -= topic_len;

    const int qos = (fixed_header >> 1) & 0x03;
    if (qos > 0) {
        if (remaining < 2 || !drainBytes(sock, 2)) {
            return false;
        }
        remaining -= 2;
    }

    const char* field = fieldFromTopic(topic);
    if (field && strcmp(field, "cover") == 0) {
        if (remaining <= 0 || remaining > kMaxCoverBytes) {
            return drainBytes(sock, remaining);
        }
        uint8_t* cover = static_cast<uint8_t*>(heap_caps_malloc(remaining, MALLOC_CAP_8BIT));
        if (!cover) {
            return drainBytes(sock, remaining);
        }
        if (!readExact(sock, cover, remaining)) {
            heap_caps_free(cover);
            return false;
        }
        updateCover(cover, static_cast<uint32_t>(remaining));
        return true;
    }

    char payload[kPayloadLimit];
    const int copy_len = remaining < kPayloadLimit - 1 ? remaining : kPayloadLimit - 1;
    if (copy_len > 0 && !readExact(sock, reinterpret_cast<uint8_t*>(payload), copy_len)) {
        return false;
    }
    payload[copy_len] = '\0';
    remaining -= copy_len;

    updateState(field, payload, static_cast<size_t>(copy_len));
    return drainBytes(sock, remaining);
}

bool mqttLoop(int sock)
{
    const int byte = readByte(sock);
    if (byte < 0) {
        const uint8_t ping[] = {0xc0, 0x00};
        return writeAll(sock, ping, sizeof(ping));
    }

    int remaining = 0;
    if (!readRemainingLength(sock, &remaining)) {
        return false;
    }

    const uint8_t packet_type = static_cast<uint8_t>(byte) >> 4;
    if (packet_type == 3) {
        return handlePublish(sock, static_cast<uint8_t>(byte), remaining);
    }
    return drainBytes(sock, remaining);
}

void applyOfflineFixture()
{
    static const char* titles[] = {"退出", "富士山下", "年少心动雨季", "What’s Going On...?"};
    static const char* artists[] = {"夏口音", "陈奕迅", "黄霄雲", "Marvin Gaye"};
    static const char* albums[] = {"中国传统音乐测试", "What’s Going On...?", "年少心动雨季 - Single", "Inner City Blues"};
    size_t index = 0;
    uint32_t frame = 0;

    while (true) {
        updateState("title", titles[index], strlen(titles[index]));
        updateState("artist", artists[index], strlen(artists[index]));
        updateState("album", albums[index], strlen(albums[index]));
        updateState("playing", "1", 1);

        char progress[64];
        snprintf(progress, sizeof(progress), "0/%u/%u", frame, 44100u * 240u);
        updateState("ssnc/prgr", progress, strlen(progress));

        std::this_thread::sleep_for(std::chrono::seconds(2));
        frame += 44100u * 2u;
        if (frame >= 44100u * 240u) {
            frame = 0;
            index = (index + 1) % (sizeof(titles) / sizeof(titles[0]));
        }
    }
}

void mqttThread()
{
    if (s_config.offline) {
        applyOfflineFixture();
        return;
    }

    while (true) {
        const int sock = connectSocket();
        if (sock < 0) {
            std::cerr << "MQTT connect failed, retrying " << s_config.host << ":" << s_config.port << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(kReconnectDelayMs));
            continue;
        }

        std::cerr << "MQTT connected to " << s_config.host << ":" << s_config.port << "\n";
        const bool ready = sendConnect(sock) && readConnack(sock) && sendSubscribe(sock);
        if (!ready) {
            close(sock);
            std::this_thread::sleep_for(std::chrono::milliseconds(kReconnectDelayMs));
            continue;
        }

        std::cerr << "MQTT subscribed to " << s_subscribe_topic << "\n";
        while (mqttLoop(sock)) {
        }

        std::cerr << "MQTT disconnected\n";
        close(sock);
        std::this_thread::sleep_for(std::chrono::milliseconds(kReconnectDelayMs));
    }
}

std::string normalizedTopic(const char* topic)
{
    std::string value = topic && topic[0] ? topic : "shairport/livingroom";
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value.empty() ? "shairport/livingroom" : value;
}

} // namespace

void SimMusicMqtt::configure(const Config& config)
{
    s_config = config;
    const std::string base = normalizedTopic(config.topic);
    s_topic_prefix = base + "/";
    s_subscribe_topic = base + "/#";
}

SimMusicMqtt::Config SimMusicMqtt::parseArgs(int argc, char** argv)
{
    Config config;
    const char* env_pass = getenv("SHAIRPORT_MQTT_PASSWORD");
    if (env_pass) {
        config.password = env_pass;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                printUsage(argv[0]);
                exit(2);
            }
            return argv[++i];
        };

        if (arg == "--mqtt-host") {
            config.host = next();
        } else if (arg == "--mqtt-port") {
            config.port = static_cast<uint16_t>(atoi(next()));
        } else if (arg == "--mqtt-user") {
            config.username = next();
        } else if (arg == "--mqtt-pass") {
            config.password = next();
        } else if (arg == "--topic") {
            config.topic = next();
        } else if (arg == "--screenshot") {
            config.screenshot_path = next();
        } else if (arg == "--run-ms") {
            config.run_ms = atoi(next());
        } else if (arg == "--recreate-at-ms") {
            config.recreate_at_ms = atoi(next());
        } else if (arg == "--offline") {
            config.offline = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            exit(0);
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            printUsage(argv[0]);
            exit(2);
        }
    }
    return config;
}

void SimMusicMqtt::printUsage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0 << " [options]\n"
        << "  --mqtt-host HOST   default 192.168.31.100\n"
        << "  --mqtt-port PORT   default 1883\n"
        << "  --mqtt-user USER   default mqtt\n"
        << "  --mqtt-pass PASS   or set SHAIRPORT_MQTT_PASSWORD\n"
        << "  --topic TOPIC      default shairport/livingroom\n"
        << "  --screenshot PATH  save the final framebuffer as BMP\n"
        << "  --run-ms MS        exit after this many milliseconds\n"
        << "  --recreate-at-ms MS destroy and recreate the music screen once\n"
        << "  --offline          run generated sample metadata instead of MQTT\n";
}

void MusicMqtt::init()
{
    bool expected = false;
    if (s_started.compare_exchange_strong(expected, true)) {
        std::thread(mqttThread).detach();
    }
}

bool MusicMqtt::getState(MusicState* state)
{
    if (!state) {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_state_mutex);
    *state = s_state;
    return s_has_state;
}

bool MusicMqtt::takeCover(CoverImage* cover)
{
    if (!cover) {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_cover_mutex);
    if (!s_pending_cover_data || s_pending_cover_size == 0) {
        return false;
    }
    cover->data = s_pending_cover_data;
    cover->size = s_pending_cover_size;
    s_pending_cover_data = nullptr;
    s_pending_cover_size = 0;
    return true;
}

bool MusicMqtt::copyLastCover(CoverImage* cover)
{
    if (!cover) {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_cover_mutex);
    if (!s_last_cover_data || s_last_cover_size == 0) {
        return false;
    }
    uint8_t* copy = static_cast<uint8_t*>(heap_caps_malloc(s_last_cover_size, MALLOC_CAP_8BIT));
    if (!copy) {
        return false;
    }
    memcpy(copy, s_last_cover_data, s_last_cover_size);
    cover->data = copy;
    cover->size = s_last_cover_size;
    return true;
}
