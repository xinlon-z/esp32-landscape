#include "clock_net.h"

#include <atomic>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "clock_secrets.h"
#include "i2c_equipment.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

namespace {

constexpr const char *kTag = "clock_net";
constexpr const char *kNtpServers[] = {
    "ntp.aliyun.com",
    "ntp.tencent.com",
    "pool.ntp.org",
};
constexpr uint32_t kNtpEpochDelta = 2208988800UL;
constexpr int kNtpRetryPerServer = 3;

constexpr EventBits_t kWifiConnectedBit = BIT0;
constexpr int kMaxRetries = 8;

EventGroupHandle_t s_wifi_event_group = nullptr;
int s_retry_num = 0;
std::atomic_bool s_wifi_connected{false};
std::atomic_bool s_ntp_synced{false};
std::atomic_bool s_sync_in_progress{false};
std::atomic_bool s_wifi_initialized{false};
std::atomic_bool s_wifi_started{false};
std::atomic_bool s_wifi_sleep_paused{false};
TaskHandle_t s_sync_task = nullptr;

void set_rtc_from_system_time()
{
    time_t now = 0;
    time(&now);

    tm local = {};
    localtime_r(&now, &local);
    if (local.tm_year < 124) {
        return;
    }

    i2c_rtc_setTime(static_cast<uint16_t>(local.tm_year + 1900),
                    static_cast<uint8_t>(local.tm_mon + 1),
                    static_cast<uint8_t>(local.tm_mday),
                    static_cast<uint8_t>(local.tm_hour),
                    static_cast<uint8_t>(local.tm_min),
                    static_cast<uint8_t>(local.tm_sec));
}

bool try_sync_time(const char *server)
{
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    addrinfo *results = nullptr;
    int dns_err = getaddrinfo(server, "123", &hints, &results);
    if (dns_err != 0 || results == nullptr) {
        ESP_LOGW(kTag, "NTP resolve failed for %s: %d", server, dns_err);
        return false;
    }

    bool synced = false;
    for (addrinfo *addr = results; addr != nullptr && !synced; addr = addr->ai_next) {
        char ip_text[INET_ADDRSTRLEN] = {};
        sockaddr_in *ipv4 = reinterpret_cast<sockaddr_in *>(addr->ai_addr);
        inet_ntop(AF_INET, &ipv4->sin_addr, ip_text, sizeof(ip_text));
        ESP_LOGI(kTag, "NTP request: %s (%s)", server, ip_text);

        for (int retry = 0; retry < kNtpRetryPerServer && !synced; ++retry) {
            int sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
            if (sock < 0) {
                ESP_LOGW(kTag, "NTP socket failed");
                continue;
            }

            timeval timeout = {};
            timeout.tv_sec = 5;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

            uint8_t packet[48] = {};
            packet[0] = 0x1b;
            ssize_t sent = sendto(sock, packet, sizeof(packet), 0, addr->ai_addr, addr->ai_addrlen);
            if (sent != sizeof(packet)) {
                ESP_LOGW(kTag, "NTP send failed to %s (%s)", server, ip_text);
                close(sock);
                continue;
            }

            sockaddr_storage source = {};
            socklen_t source_len = sizeof(source);
            ssize_t received = recvfrom(sock, packet, sizeof(packet), 0, reinterpret_cast<sockaddr *>(&source), &source_len);
            close(sock);

            if (received < 48) {
                ESP_LOGI(kTag, "NTP wait timeout from %s (%s), try %d/%d", server, ip_text, retry + 1, kNtpRetryPerServer);
                continue;
            }

            const uint8_t mode = packet[0] & 0x07;
            const uint8_t stratum = packet[1];
            uint32_t seconds = (static_cast<uint32_t>(packet[40]) << 24) |
                               (static_cast<uint32_t>(packet[41]) << 16) |
                               (static_cast<uint32_t>(packet[42]) << 8) |
                               static_cast<uint32_t>(packet[43]);
            uint32_t fraction = (static_cast<uint32_t>(packet[44]) << 24) |
                                (static_cast<uint32_t>(packet[45]) << 16) |
                                (static_cast<uint32_t>(packet[46]) << 8) |
                                static_cast<uint32_t>(packet[47]);

            if (mode != 4 || stratum == 0 || seconds <= kNtpEpochDelta) {
                ESP_LOGW(kTag, "NTP invalid response from %s (%s)", server, ip_text);
                continue;
            }

            timeval tv = {};
            tv.tv_sec = static_cast<time_t>(seconds - kNtpEpochDelta);
            tv.tv_usec = static_cast<suseconds_t>((static_cast<uint64_t>(fraction) * 1000000ULL) >> 32);
            if (settimeofday(&tv, nullptr) != 0) {
                ESP_LOGW(kTag, "settimeofday failed after NTP response");
                continue;
            }

            s_ntp_synced.store(true, std::memory_order_relaxed);
            set_rtc_from_system_time();
            ESP_LOGI(kTag, "NTP synced via %s (%s)", server, ip_text);
            synced = true;
        }
    }

    freeaddrinfo(results);
    return synced;
}

void wifi_event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_wifi_started.store(true, std::memory_order_relaxed);
        if (!s_wifi_sleep_paused.load(std::memory_order_relaxed)) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_STOP) {
        s_wifi_started.store(false, std::memory_order_relaxed);
        s_wifi_connected.store(false, std::memory_order_relaxed);
        xEventGroupClearBits(s_wifi_event_group, kWifiConnectedBit);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected.store(false, std::memory_order_relaxed);
        xEventGroupClearBits(s_wifi_event_group, kWifiConnectedBit);
        if (s_wifi_sleep_paused.load(std::memory_order_relaxed)) {
            ESP_LOGI(kTag, "WiFi disconnected for app sleep");
            return;
        }
        ++s_retry_num;
        esp_wifi_connect();
        if ((s_retry_num % kMaxRetries) == 0) {
            ESP_LOGW(kTag, "WiFi still reconnecting after %d attempts", s_retry_num);
        } else {
            ESP_LOGI(kTag, "WiFi reconnect attempt %d", s_retry_num);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = static_cast<ip_event_got_ip_t *>(event_data);
        ESP_LOGI(kTag, "WiFi got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_wifi_connected.store(true, std::memory_order_relaxed);
        xEventGroupSetBits(s_wifi_event_group, kWifiConnectedBit);
    }
}

void sync_time_task(void *)
{
    setenv("TZ", "CST-8", 1);
    tzset();

    while (true) {
        if (s_wifi_sleep_paused.load(std::memory_order_relaxed)) {
            s_sync_in_progress.store(false, std::memory_order_relaxed);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (s_ntp_synced.load(std::memory_order_relaxed)) {
            break;
        }

        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                               kWifiConnectedBit,
                                               pdFALSE,
                                               pdFALSE,
                                               pdMS_TO_TICKS(30000));

        if ((bits & kWifiConnectedBit) == 0) {
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        s_sync_in_progress.store(true, std::memory_order_relaxed);
        for (const char *server : kNtpServers) {
            if (try_sync_time(server)) {
                break;
            }
        }
        s_sync_in_progress.store(false, std::memory_order_relaxed);

        if (!s_ntp_synced.load(std::memory_order_relaxed)) {
            ESP_LOGW(kTag, "NTP sync timed out, retrying later");
            vTaskDelay(pdMS_TO_TICKS(10 * 60 * 1000));
        }
    }

    s_sync_task = nullptr;
    vTaskDelete(nullptr);
}

void start_sync_task_if_needed()
{
    if (s_sync_task != nullptr) {
        return;
    }
    BaseType_t created = xTaskCreatePinnedToCore(sync_time_task, "clock_sntp", 4096, nullptr, 3, &s_sync_task, 0);
    if (created != pdPASS) {
        s_sync_task = nullptr;
        ESP_LOGW(kTag, "Failed to create NTP sync task");
    }
}

void start_wifi_station()
{
    if (!s_wifi_initialized.load(std::memory_order_relaxed)) {
        return;
    }

    esp_err_t ret = esp_wifi_start();
    if (ret == ESP_OK) {
        s_wifi_started.store(true, std::memory_order_relaxed);
        return;
    }

    ESP_LOGW(kTag, "WiFi start returned %s", esp_err_to_name(ret));
    if (s_wifi_started.load(std::memory_order_relaxed)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
    }
}

void network_task(void *)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, nullptr));

    wifi_config_t wifi_config = {};
    strlcpy(reinterpret_cast<char *>(wifi_config.sta.ssid), kWifiSsid, sizeof(wifi_config.sta.ssid));
    strlcpy(reinterpret_cast<char *>(wifi_config.sta.password), kWifiPassword, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    s_wifi_initialized.store(true, std::memory_order_relaxed);
    start_wifi_station();
    ESP_LOGI(kTag, "WiFi start");

    start_sync_task_if_needed();
    vTaskDelete(nullptr);
}

} // namespace

void ClockNet::init()
{
    if (s_wifi_event_group != nullptr) {
        return;
    }
    s_wifi_event_group = xEventGroupCreate();
    xTaskCreatePinnedToCore(network_task, "clock_net", 4096, nullptr, 3, nullptr, 0);
}

void ClockNet::pauseForSleep()
{
    s_wifi_sleep_paused.store(true, std::memory_order_relaxed);
    s_sync_in_progress.store(false, std::memory_order_relaxed);
    s_wifi_connected.store(false, std::memory_order_relaxed);
    if (s_wifi_event_group != nullptr) {
        xEventGroupClearBits(s_wifi_event_group, kWifiConnectedBit);
    }
    if (s_wifi_initialized.load(std::memory_order_relaxed) &&
        s_wifi_started.load(std::memory_order_relaxed)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_disconnect());
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_stop());
    }
}

void ClockNet::requestSync()
{
    s_wifi_sleep_paused.store(false, std::memory_order_relaxed);
    s_ntp_synced.store(false, std::memory_order_relaxed);
    s_sync_in_progress.store(false, std::memory_order_relaxed);

    if (s_wifi_event_group == nullptr) {
        init();
        return;
    }

    start_sync_task_if_needed();
    start_wifi_station();
}

ClockNet::Status ClockNet::getStatus()
{
    Status status;
    status.wifi_connected   = s_wifi_connected.load(std::memory_order_relaxed);
    status.ntp_synced       = s_ntp_synced.load(std::memory_order_relaxed);
    status.sync_in_progress = s_sync_in_progress.load(std::memory_order_relaxed);
    return status;
}
