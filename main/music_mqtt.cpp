#include "music_mqtt.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "clock_net.h"
#include "clock_secrets.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/netdb.h"

namespace {

constexpr const char* kTag = "music_mqtt";
constexpr const char* kTopicPrefix = "shairport/livingroom/";
constexpr const char* kSubscribeTopic = "shairport/livingroom/#";
constexpr const char* kClientId = "esp32-music-ui";
constexpr int kSocketTimeoutSeconds = 20;
constexpr int kReconnectDelayMs = 5000;
constexpr int kTaskStackBytes = 6144;
constexpr int kPayloadLimit = 256;
constexpr int kMaxCoverBytes = 256 * 1024;

SemaphoreHandle_t s_state_mutex = nullptr;
SemaphoreHandle_t s_cover_mutex = nullptr;
TaskHandle_t s_task = nullptr;
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
    const ssize_t got = recv(sock, &byte, 1, 0);
    if (got == 1) {
        return byte;
    }
    return -1;
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

bool sendConnect(int sock)
{
    uint8_t body[256];
    size_t pos = 0;
    pos = appendString(body, pos, "MQTT");
    body[pos++] = 4;
    body[pos++] = 0xc2;
    body[pos++] = 0;
    body[pos++] = 60;
    pos = appendString(body, pos, kClientId);
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

bool sendSubscribe(int sock)
{
    uint8_t body[160];
    size_t pos = 0;
    body[pos++] = 0;
    body[pos++] = 1;
    pos = appendString(body, pos, kSubscribeTopic);
    body[pos++] = 0;

    uint8_t packet[180];
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

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        musicStateApplyField(s_state, field, payload, payload_len);
        if (strcmp(field, "ssnc/prgr") == 0) {
            s_state.last_progress_ms = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        }
        s_has_state = true;
        xSemaphoreGive(s_state_mutex);
    }

    if (strcmp(field, "title") == 0 || strcmp(field, "artist") == 0 ||
        strcmp(field, "album") == 0 || strcmp(field, "active") == 0 ||
        strcmp(field, "playing") == 0 || strcmp(field, "ssnc/prgr") == 0) {
        ESP_LOGI(kTag, "state %s=%.*s", field, static_cast<int>(payload_len), payload);
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

uint8_t* copyCover(const uint8_t* data, uint32_t size)
{
    if (!data || size == 0 || size > kMaxCoverBytes) {
        return nullptr;
    }
    uint8_t* copy = allocCover(static_cast<int>(size));
    if (copy) {
        memcpy(copy, data, size);
    }
    return copy;
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

    uint8_t* last_copy = copyCover(data, size);
    if (xSemaphoreTake(s_cover_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
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
        xSemaphoreGive(s_cover_mutex);
        ESP_LOGI(kTag, "cover received: %lu bytes", static_cast<unsigned long>(size));
    } else {
        heap_caps_free(data);
        if (last_copy) {
            heap_caps_free(last_copy);
        }
    }
}

bool handlePublish(int sock, uint8_t fixed_header, int remaining)
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
    if (field && strcmp(field, "cover") == 0) {
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

void mqttTask(void*)
{
    while (true) {
        while (!ClockNet::getStatus().wifi_connected) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        int sock = connectSocket();
        if (sock < 0) {
            ESP_LOGW(kTag, "connect failed");
            vTaskDelay(pdMS_TO_TICKS(kReconnectDelayMs));
            continue;
        }

        ESP_LOGI(kTag, "connected to %s:%d", kMqttHost, kMqttPort);
        const bool ready = sendConnect(sock) && readConnack(sock) && sendSubscribe(sock);
        if (!ready) {
            ESP_LOGW(kTag, "MQTT handshake failed");
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(kReconnectDelayMs));
            continue;
        }

        ESP_LOGI(kTag, "subscribed to %s", kSubscribeTopic);
        while (ClockNet::getStatus().wifi_connected && mqttLoop(sock)) {
        }

        ESP_LOGW(kTag, "disconnected");
        close(sock);
        vTaskDelay(pdMS_TO_TICKS(kReconnectDelayMs));
    }
}

} // namespace

void MusicMqtt::init()
{
    if (!s_state_mutex) {
        s_state_mutex = xSemaphoreCreateMutex();
    }
    if (!s_cover_mutex) {
        s_cover_mutex = xSemaphoreCreateMutex();
    }
    if (!s_task) {
        xTaskCreatePinnedToCore(mqttTask, "music_mqtt", kTaskStackBytes, nullptr, 3, &s_task, 0);
    }
}

bool MusicMqtt::getState(MusicState* state)
{
    if (!state || !s_state_mutex) {
        return false;
    }

    bool has_state = false;
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        *state = s_state;
        has_state = s_has_state;
        xSemaphoreGive(s_state_mutex);
    }
    return has_state;
}

bool MusicMqtt::takeCover(CoverImage* cover)
{
    if (!cover || !s_cover_mutex) {
        return false;
    }

    bool has_cover = false;
    if (xSemaphoreTake(s_cover_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (s_pending_cover_data && s_pending_cover_size > 0) {
            cover->data = s_pending_cover_data;
            cover->size = s_pending_cover_size;
            s_pending_cover_data = nullptr;
            s_pending_cover_size = 0;
            has_cover = true;
        }
        xSemaphoreGive(s_cover_mutex);
    }
    return has_cover;
}

bool MusicMqtt::copyLastCover(CoverImage* cover)
{
    if (!cover || !s_cover_mutex) {
        return false;
    }

    bool has_cover = false;
    if (xSemaphoreTake(s_cover_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (s_last_cover_data && s_last_cover_size > 0) {
            uint8_t* copy = copyCover(s_last_cover_data, s_last_cover_size);
            if (copy) {
                cover->data = copy;
                cover->size = s_last_cover_size;
                has_cover = true;
            }
        }
        xSemaphoreGive(s_cover_mutex);
    }
    return has_cover;
}
