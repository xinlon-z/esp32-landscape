#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "clock_net.h"
#include "music_mqtt.h"
#include "clock_secrets.h"
#include "app/features/music/util/music_trace.h"
#include "app/services/mqtt_service.h"
#include "app/services/cover_service.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lwip/netdb.h"

namespace {

constexpr const char* kTag = "music_mqtt";
constexpr const char* kTopicPrefix = "shairport/livingroom/";
constexpr const char* kMetadataClientId = "esp32-music-meta";
constexpr const char* kCoverClientId = "esp32-music-cover";
constexpr const char* kMetadataTopics[] = {
    "shairport/livingroom/title",
    "shairport/livingroom/artist",
    "shairport/livingroom/album",
    "shairport/livingroom/playing",
    "shairport/livingroom/ssnc/prgr",
};
constexpr const char* kCoverTopics[] = {
    "shairport/livingroom/cover",
};
constexpr int kSocketTimeoutSeconds = 5;
constexpr uint32_t kPingIntervalMs = 25000;
constexpr uint32_t kPingResponseTimeoutMs = 30000;
constexpr int kReconnectDelayMs = 5000;
constexpr int kMetadataTaskStackBytes = 6144;
constexpr int kCoverTaskStackBytes = 6144;
constexpr int kPayloadLimit = 256;
constexpr int kMaxCoverBytes = 256 * 1024;
constexpr UBaseType_t kMetadataTaskPriority = 5;
constexpr UBaseType_t kCoverTaskPriority = 1;
constexpr BaseType_t kMetadataTaskCore = 0;
constexpr BaseType_t kCoverTaskCore = 1;

enum class StreamKind : uint8_t {
    Metadata,
    Cover,
};

struct StreamConfig {
    const char* name;
    const char* client_id;
    const char* const* topics;
    size_t topic_count;
    StreamKind kind;
};

struct MqttKeepalive {
    uint32_t last_tx_ms = 0;
    uint32_t ping_sent_ms = 0;
    bool ping_outstanding = false;
};

enum class ReadByteStatus : uint8_t {
    Byte,
    Timeout,
    Closed,
};

struct ReadByteResult {
    ReadByteStatus status = ReadByteStatus::Closed;
    uint8_t byte = 0;
};

constexpr StreamConfig kMetadataStream = {
    "metadata",
    kMetadataClientId,
    kMetadataTopics,
    sizeof(kMetadataTopics) / sizeof(kMetadataTopics[0]),
    StreamKind::Metadata,
};

constexpr StreamConfig kCoverStream = {
    "cover",
    kCoverClientId,
    kCoverTopics,
    sizeof(kCoverTopics) / sizeof(kCoverTopics[0]),
    StreamKind::Cover,
};

TaskHandle_t s_metadata_task = nullptr;
TaskHandle_t s_cover_task = nullptr;

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

uint32_t mqttNowMs()
{
    return static_cast<uint32_t>(xTaskGetTickCount()) * portTICK_PERIOD_MS;
}

bool elapsedAtLeast(uint32_t now_ms, uint32_t then_ms, uint32_t interval_ms)
{
    return static_cast<uint32_t>(now_ms - then_ms) >= interval_ms;
}

void noteClientTx(MqttKeepalive* keepalive, uint32_t now_ms)
{
    if (!keepalive) {
        return;
    }
    keepalive->last_tx_ms = now_ms;
}

void notePingSent(MqttKeepalive* keepalive, uint32_t now_ms)
{
    if (!keepalive) {
        return;
    }
    keepalive->last_tx_ms = now_ms;
    keepalive->ping_sent_ms = now_ms;
    keepalive->ping_outstanding = true;
}

void notePingResp(MqttKeepalive* keepalive)
{
    if (!keepalive) {
        return;
    }
    keepalive->ping_outstanding = false;
}

bool keepalivePingDue(const MqttKeepalive& keepalive, uint32_t now_ms)
{
    return !keepalive.ping_outstanding &&
           elapsedAtLeast(now_ms, keepalive.last_tx_ms, kPingIntervalMs);
}

bool keepaliveResponseExpired(const MqttKeepalive& keepalive, uint32_t now_ms)
{
    return keepalive.ping_outstanding &&
           elapsedAtLeast(now_ms, keepalive.ping_sent_ms, kPingResponseTimeoutMs);
}

bool sendPingReq(int sock, MqttKeepalive* keepalive)
{
    const uint8_t ping[] = {0xc0, 0x00};
    if (!writeAll(sock, ping, sizeof(ping))) {
        return false;
    }
    notePingSent(keepalive, mqttNowMs());
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

ReadByteResult readByte(int sock)
{
    uint8_t byte = 0;
    errno = 0;
    const ssize_t got = recv(sock, &byte, 1, 0);
    if (got == 1) {
        return ReadByteResult{ReadByteStatus::Byte, byte};
    }
    if (got == 0) {
        return ReadByteResult{ReadByteStatus::Closed, 0};
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return ReadByteResult{ReadByteStatus::Timeout, 0};
    }
    return ReadByteResult{ReadByteStatus::Closed, 0};
}

size_t appendString(uint8_t* packet, size_t pos, const char* text)
{
    const size_t len = strlen(text);
    packet[pos++] = static_cast<uint8_t>((len >> 8) & 0xff);
    packet[pos++] = static_cast<uint8_t>(len & 0xff);
    memcpy(packet + pos, text, len);
    return pos + len;
}

size_t encodeRemainingLength(uint8_t* packet, size_t pos, size_t len)
{
    do {
        uint8_t byte = len % 128;
        len /= 128;
        if (len > 0) {
            byte |= 0x80;
        }
        packet[pos++] = byte;
    } while (len > 0);
    return pos;
}

bool readRemainingLength(int sock, int* remaining)
{
    int multiplier = 1;
    int value = 0;

    for (int i = 0; i < 4; ++i) {
        const ReadByteResult read = readByte(sock);
        if (read.status != ReadByteStatus::Byte) {
            return false;
        }
        const int encoded = read.byte;
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
    uint8_t buffer[128];
    while (len > 0) {
        const int chunk = len > static_cast<int>(sizeof(buffer)) ? static_cast<int>(sizeof(buffer)) : len;
        if (!readExact(sock, buffer, chunk)) {
            return false;
        }
        len -= chunk;
    }
    return true;
}

bool sendConnect(int sock, const char* client_id)
{
    uint8_t body[256];
    size_t pos = 0;
    pos = appendString(body, pos, "MQTT");
    body[pos++] = 4;
    body[pos++] = 0xc2;
    body[pos++] = 0;
    body[pos++] = 60;
    pos = appendString(body, pos, client_id);
    pos = appendString(body, pos, kMqttUsername);
    pos = appendString(body, pos, kMqttPassword);

    uint8_t packet[300];
    size_t out = 0;
    packet[out++] = 0x10;
    out = encodeRemainingLength(packet, out, pos);
    memcpy(packet + out, body, pos);
    out += pos;
    return writeAll(sock, packet, out);
}

bool readConnack(int sock)
{
    uint8_t response[4];
    if (!readExact(sock, response, sizeof(response))) {
        return false;
    }
    return response[0] == 0x20 && response[1] == 0x02 && response[3] == 0x00;
}

bool appendSubscription(uint8_t* body, size_t body_size, size_t* pos, const char* topic)
{
    const size_t len = strlen(topic);
    if (*pos + 2 + len + 1 > body_size) {
        return false;
    }

    body[(*pos)++] = static_cast<uint8_t>((len >> 8) & 0xff);
    body[(*pos)++] = static_cast<uint8_t>(len & 0xff);
    memcpy(body + *pos, topic, len);
    *pos += len;
    body[(*pos)++] = 0;
    return true;
}

bool sendSubscribe(int sock, const char* const* topics, size_t topic_count)
{
    uint8_t body[384];
    size_t pos = 0;
    body[pos++] = 0;
    body[pos++] = 1;
    for (size_t i = 0; i < topic_count; ++i) {
        if (!appendSubscription(body, sizeof(body), &pos, topics[i])) {
            return false;
        }
    }

    uint8_t packet[420];
    size_t out = 0;
    packet[out++] = 0x82;
    out = encodeRemainingLength(packet, out, pos);
    memcpy(packet + out, body, pos);
    out += pos;
    return writeAll(sock, packet, out);
}

int connectSocket()
{
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port[8];
    snprintf(port, sizeof(port), "%d", kMqttPort);
    addrinfo* results = nullptr;
    if (getaddrinfo(kMqttHost, port, &hints, &results) != 0 || !results) {
        return -1;
    }

    int sock = -1;
    for (addrinfo* addr = results; addr != nullptr; addr = addr->ai_next) {
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
    const size_t prefix_len = strlen(kTopicPrefix);
    if (strncmp(topic, kTopicPrefix, prefix_len) != 0) {
        return nullptr;
    }
    return topic + prefix_len;
}

void updateState(const char* field, const char* payload, size_t payload_len)
{
    if (!field || strcmp(field, "cover") == 0) {
        return;
    }

    const uint32_t progress_ms = strcmp(field, "ssnc/prgr") == 0 ? lv_tick_get() : 0;
    MqttService::get().applyField(field, payload, payload_len, progress_ms);

    if (strcmp(field, "title") == 0 || strcmp(field, "artist") == 0 ||
        strcmp(field, "album") == 0 || strcmp(field, "active") == 0 ||
        strcmp(field, "playing") == 0 || strcmp(field, "ssnc/prgr") == 0) {
        MUSIC_TRACE_LOGI(kTag, "state %s=%.*s", field, static_cast<int>(payload_len), payload);
    }
}

uint8_t* allocCover(int len)
{
    uint8_t* data = static_cast<uint8_t*>(heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!data) {
        data = static_cast<uint8_t*>(heap_caps_malloc(len, MALLOC_CAP_8BIT));
    }
    return data;
}

bool isJpegCover(const uint8_t* data, uint32_t size)
{
    return data && size >= 128 && data[0] == 0xff && data[1] == 0xd8;
}

void updateCover(uint8_t* data, uint32_t size)
{
    if (!data || size == 0) {
        return;
    }

    if (!isJpegCover(data, size)) {
        ESP_LOGI(kTag, "ignoring non-JPEG cover payload: %lu bytes", static_cast<unsigned long>(size));
        heap_caps_free(data);
        return;
    }

    // CoverService takes ownership of the original payload; the UI reads the
    // decoded result from its service snapshot.
    CoverService::get().acceptJpeg(data, size);
    ESP_LOGI(kTag, "cover received: %lu bytes", static_cast<unsigned long>(size));
}

bool handlePublish(int sock, uint8_t fixed_header, int remaining, StreamKind kind)
{
    uint8_t len_buf[2];
    if (remaining < 2 || !readExact(sock, len_buf, sizeof(len_buf))) {
        return false;
    }
    remaining -= 2;

    const int topic_len = (static_cast<int>(len_buf[0]) << 8) | len_buf[1];
    if (topic_len <= 0 || topic_len > 160 || remaining < topic_len) {
        return drainBytes(sock, remaining);
    }

    char topic[161];
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
    if (!field) {
        return drainBytes(sock, remaining);
    }

    if (field && strcmp(field, "cover") == 0) {
        if (kind != StreamKind::Cover) {
            return drainBytes(sock, remaining);
        }
        if (remaining <= 0 || remaining > kMaxCoverBytes) {
            return drainBytes(sock, remaining);
        }

        uint8_t* cover = allocCover(remaining);
        if (!cover) {
            ESP_LOGW(kTag, "cover allocation failed for %d bytes", remaining);
            return drainBytes(sock, remaining);
        }
        if (!readExact(sock, cover, remaining)) {
            heap_caps_free(cover);
            return false;
        }
        updateCover(cover, static_cast<uint32_t>(remaining));
        return true;
    }

    if (kind != StreamKind::Metadata) {
        return drainBytes(sock, remaining);
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

bool mqttLoop(int sock, StreamKind kind, MqttKeepalive* keepalive)
{
    const uint32_t now_ms = mqttNowMs();
    if (keepaliveResponseExpired(*keepalive, now_ms)) {
        ESP_LOGW(kTag, "PINGRESP timeout");
        return false;
    }
    if (keepalivePingDue(*keepalive, now_ms) && !sendPingReq(sock, keepalive)) {
        ESP_LOGW(kTag, "PINGREQ send failed");
        return false;
    }

    const ReadByteResult first = readByte(sock);
    if (first.status == ReadByteStatus::Timeout) {
        return true;
    }
    if (first.status == ReadByteStatus::Closed) {
        return false;
    }

    int remaining = 0;
    if (!readRemainingLength(sock, &remaining)) {
        return false;
    }

    const uint8_t packet_type = first.byte >> 4;
    if (packet_type == 3) {
        return handlePublish(sock, first.byte, remaining, kind);
    }
    if (packet_type == 13) {
        notePingResp(keepalive);
    }
    return drainBytes(sock, remaining);
}

void mqttTask(void* arg)
{
    const auto* config = static_cast<const StreamConfig*>(arg);

    while (true) {
        while (!ClockNet::getStatus().wifi_connected) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        int sock = connectSocket();
        if (sock < 0) {
            ESP_LOGW(kTag, "%s connect failed", config->name);
            vTaskDelay(pdMS_TO_TICKS(kReconnectDelayMs));
            continue;
        }

        ESP_LOGI(kTag, "%s connected to %s:%d", config->name, kMqttHost, kMqttPort);
        MqttKeepalive keepalive{};
        bool ready = sendConnect(sock, config->client_id);
        if (ready) {
            noteClientTx(&keepalive, mqttNowMs());
            ready = readConnack(sock);
        }
        if (ready) {
            ready = sendSubscribe(sock, config->topics, config->topic_count);
            if (ready) {
                noteClientTx(&keepalive, mqttNowMs());
            }
        }
        if (!ready) {
            ESP_LOGW(kTag, "%s MQTT handshake failed", config->name);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(kReconnectDelayMs));
            continue;
        }

        ESP_LOGI(kTag, "%s subscribed to %u topic(s)",
                 config->name, static_cast<unsigned>(config->topic_count));
        while (ClockNet::getStatus().wifi_connected && mqttLoop(sock, config->kind, &keepalive)) {
        }

        ESP_LOGW(kTag, "%s disconnected", config->name);
        close(sock);
        vTaskDelay(pdMS_TO_TICKS(kReconnectDelayMs));
    }
}

} // namespace

void MusicMqtt::init()
{
    if (!s_metadata_task) {
        xTaskCreatePinnedToCore(mqttTask, "music_meta", kMetadataTaskStackBytes,
                                const_cast<StreamConfig*>(&kMetadataStream),
                                kMetadataTaskPriority, &s_metadata_task,
                                kMetadataTaskCore);
    }
    if (!s_cover_task) {
        xTaskCreatePinnedToCore(mqttTask, "music_cover", kCoverTaskStackBytes,
                                const_cast<StreamConfig*>(&kCoverStream),
                                kCoverTaskPriority, &s_cover_task,
                                kCoverTaskCore);
    }
}
