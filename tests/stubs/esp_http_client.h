#pragma once

#include "esp_err.h"

#include <stdint.h>
#include <string.h>

struct EspHttpClientStubHandle {
    int unused = 0;
};

typedef EspHttpClientStubHandle* esp_http_client_handle_t;

typedef enum {
    HTTP_EVENT_ON_HEADER,
} esp_http_client_event_id_t;

typedef struct esp_http_client_event {
    esp_http_client_event_id_t event_id;
    esp_http_client_handle_t client;
    void* user_data;
    char* header_key;
    char* header_value;
} esp_http_client_event_t;

typedef int (*http_event_handle_cb)(esp_http_client_event_t* evt);

struct esp_http_client_config_t {
    const char* url = nullptr;
    int timeout_ms = 0;
    bool disable_auto_redirect = false;
    int max_redirection_count = 0;
    http_event_handle_cb event_handler = nullptr;
    void* user_data = nullptr;
};

struct EspHttpClientStubHeader {
    const char* key = nullptr;
    const char* value = nullptr;
};

struct EspHttpClientStubResponse {
    int status_code = 0;
    int64_t content_length = -1;
    const uint8_t* body = nullptr;
    uint32_t body_size = 0;
    EspHttpClientStubHeader headers[8]{};
};

struct EspHttpClientStubState {
    const char* init_url = nullptr;
    int init_timeout_ms = 0;
    bool init_disable_auto_redirect = false;
    int init_max_redirection_count = 0;
    http_event_handle_cb event_handler = nullptr;
    void* user_data = nullptr;
    bool fail_init = false;
    bool fail_open = false;
    bool fail_redirect = false;
    int open_calls = 0;
    int close_calls = 0;
    int cleanup_calls = 0;
    int fetch_headers_calls = 0;
    int status_calls = 0;
    int redirect_calls = 0;
    int read_calls = 0;
    int current_response = 0;
    uint32_t read_offset = 0;
    EspHttpClientStubResponse responses[4]{};

    void reset()
    {
        *this = EspHttpClientStubState{};
    }
};

inline EspHttpClientStubState& espHttpClientStubState()
{
    static EspHttpClientStubState state;
    return state;
}

static inline esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config)
{
    auto& state = espHttpClientStubState();
    if (state.fail_init) {
        return nullptr;
    }
    state.init_url = config ? config->url : nullptr;
    state.init_timeout_ms = config ? config->timeout_ms : 0;
    state.init_disable_auto_redirect = config ? config->disable_auto_redirect : false;
    state.init_max_redirection_count = config ? config->max_redirection_count : 0;
    state.event_handler = config ? config->event_handler : nullptr;
    state.user_data = config ? config->user_data : nullptr;
    static EspHttpClientStubHandle handle;
    return &handle;
}

static inline esp_err_t esp_http_client_open(esp_http_client_handle_t, int)
{
    auto& state = espHttpClientStubState();
    if (state.fail_open) {
        return ESP_ERR_INVALID_ARG;
    }
    state.current_response = state.open_calls;
    state.read_offset = 0;
    state.open_calls++;
    return ESP_OK;
}

static inline int64_t esp_http_client_fetch_headers(esp_http_client_handle_t)
{
    auto& state = espHttpClientStubState();
    state.fetch_headers_calls++;
    if (state.event_handler) {
        for (auto& header : state.responses[state.current_response].headers) {
            if (!header.key || !header.value) {
                continue;
            }
            esp_http_client_event_t event{};
            event.event_id = HTTP_EVENT_ON_HEADER;
            event.client = nullptr;
            event.user_data = state.user_data;
            event.header_key = const_cast<char*>(header.key);
            event.header_value = const_cast<char*>(header.value);
            state.event_handler(&event);
        }
    }
    return state.responses[state.current_response].content_length;
}

static inline int esp_http_client_get_status_code(esp_http_client_handle_t)
{
    auto& state = espHttpClientStubState();
    state.status_calls++;
    return state.responses[state.current_response].status_code;
}

static inline int esp_http_client_read(esp_http_client_handle_t, char* buffer, int len)
{
    auto& state = espHttpClientStubState();
    state.read_calls++;
    const EspHttpClientStubResponse& response = state.responses[state.current_response];
    if (!buffer || len <= 0 || !response.body || state.read_offset >= response.body_size) {
        return 0;
    }
    uint32_t read = response.body_size - state.read_offset;
    if (read > static_cast<uint32_t>(len)) {
        read = static_cast<uint32_t>(len);
    }
    memcpy(buffer, response.body + state.read_offset, read);
    state.read_offset += read;
    return static_cast<int>(read);
}

static inline esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t)
{
    auto& state = espHttpClientStubState();
    state.redirect_calls++;
    return state.fail_redirect ? ESP_ERR_INVALID_ARG : ESP_OK;
}

static inline void esp_http_client_close(esp_http_client_handle_t)
{
    espHttpClientStubState().close_calls++;
}

static inline void esp_http_client_cleanup(esp_http_client_handle_t)
{
    espHttpClientStubState().cleanup_calls++;
}
