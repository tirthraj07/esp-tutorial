#include "http_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"

#define HTTP_TIMEOUT_MS 60000
#define HTTP_EAGAIN_RETRIES 3
#define HTTP_MAX_BODY_BYTES (32 * 1024)

typedef struct {
    char* data;
    size_t len;
    size_t cap;
    bool overflow;
} HttpBodyBuffer;

static esp_err_t http_collect_event_handler(esp_http_client_event_t* evt)
{
    HttpBodyBuffer* body = (HttpBodyBuffer*)evt->user_data;
    if (body == NULL) {
        return ESP_OK;
    }

    if (evt->event_id != HTTP_EVENT_ON_DATA || evt->data == NULL || evt->data_len <= 0) {
        return ESP_OK;
    }

    if (body->overflow) {
        return ESP_OK;
    }

    size_t needed = body->len + (size_t)evt->data_len + 1; // +1 for '\0'
    if (needed > HTTP_MAX_BODY_BYTES) {
        body->overflow = true;
        return ESP_OK;
    }

    if (needed > body->cap) {
        size_t new_cap = body->cap == 0 ? 512 : body->cap;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        char* new_data = (char*)realloc(body->data, new_cap);
        if (new_data == NULL) {
            body->overflow = true;
            return ESP_OK;
        }
        body->data = new_data;
        body->cap = new_cap;
    }

    memcpy(body->data + body->len, evt->data, (size_t)evt->data_len);
    body->len += (size_t)evt->data_len;
    body->data[body->len] = '\0';
    return ESP_OK;
}

esp_err_t handle_get(const char* url, int* out_status_code, char** out_body)
{
    if (url == NULL || out_status_code == NULL || out_body == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_status_code = -1;
    *out_body = NULL;

    HttpBodyBuffer body = {0};

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_collect_event_handler,
        .user_data = &body,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(body.data);
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_http_client_set_method(client, HTTP_METHOD_GET));

    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= HTTP_EAGAIN_RETRIES; attempt++) {
        err = esp_http_client_perform(client);
        if (err == ESP_ERR_HTTP_EAGAIN && attempt < HTTP_EAGAIN_RETRIES) {
            printf("HTTP request timed out; retrying (%d/%d)...\n", attempt, HTTP_EAGAIN_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        break;
    }

    if (err == ESP_OK) {
        *out_status_code = esp_http_client_get_status_code(client);
    }

    esp_http_client_cleanup(client);

    if (body.overflow) {
        free(body.data);
        return ESP_ERR_NO_MEM;
    }

    if (body.data == NULL) {
        body.data = (char*)malloc(1);
        if (body.data == NULL) {
            return ESP_ERR_NO_MEM;
        }
        body.data[0] = '\0';
    }

    *out_body = body.data;
    return err;
}

