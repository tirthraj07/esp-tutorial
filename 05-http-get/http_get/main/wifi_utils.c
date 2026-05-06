#include "wifi_utils.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"

static EventGroupHandle_t s_wifi_event_group = NULL;
static const EventBits_t WIFI_HAS_IP_BIT = BIT0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        printf("Wi-Fi Driver Started! Connecting...\n");
        ESP_ERROR_CHECK(esp_wifi_connect());
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        printf("Disconnected from router. Retrying...\n");
        if (s_wifi_event_group != NULL) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_HAS_IP_BIT);
        }
        ESP_ERROR_CHECK(esp_wifi_connect());
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        printf("\nSUCCESS! Connected to Wi-Fi! IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
        if (s_wifi_event_group != NULL) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_HAS_IP_BIT);
        }
    }
}

void configure_wifi(const char* ssid, const char* password)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            // Note: .ssid / .password are fixed-size arrays, so we copy below.
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };

    // Copy credentials into struct's internal buffers.
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
}

void register_wifi_event_handlers(void)
{
    if (s_wifi_event_group == NULL) {
        s_wifi_event_group = xEventGroupCreate();
        if (s_wifi_event_group == NULL) {
            // Fail fast; without this we can't signal "IP ready" to app_main.
            abort();
        }
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));
}

bool wifi_wait_for_ip(TickType_t timeout_ticks)
{
    if (s_wifi_event_group == NULL) {
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_HAS_IP_BIT,
        pdFALSE,
        pdTRUE,
        timeout_ticks);

    return (bits & WIFI_HAS_IP_BIT) != 0;
}

