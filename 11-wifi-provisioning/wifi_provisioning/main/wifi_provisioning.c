#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

// --- NEW v6.0 Headers ---
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"

static const char *TAG = "PROV_POC";

// Security: Proof of Possession (PoP). 
const char *PROOF_OF_POSSESSION = "12345678"; 

// Event handler to watch the provisioning process
static void prov_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == NETWORK_PROV_EVENT) {
        switch (event_id) {
            case NETWORK_PROV_START:
                ESP_LOGI(TAG, "Provisioning started! Open the Espressif App.");
                break;
            case NETWORK_PROV_WIFI_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received Wi-Fi credentials! SSID: %s", (const char *)wifi_sta_cfg->ssid);
                break;
            }
            case NETWORK_PROV_WIFI_CRED_SUCCESS:
                ESP_LOGI(TAG, "Connected to AP with provisioned credentials.");
                break;
            case NETWORK_PROV_END:
                ESP_LOGI(TAG, "Provisioning complete. BLE shutting down.");
                break;
            default:
                break;
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "SUCCESS! Connected with IP: " IPSTR, IP2STR(&event->ip_info.ip));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected. Retrying...");
        esp_wifi_connect();
    }
}

void app_main(void) {
    // 1. Initialize NVS (Crucial! This is where credentials live)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // 2. Initialize TCP/IP and the Event Loop
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    // 3. Register Event Handlers (Using the new NETWORK_PROV_EVENT base)
    esp_event_handler_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler, NULL);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &prov_event_handler, NULL);

    // 4. Initialize Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // 5. Configure the Provisioning Manager (v6.0 Structs)
    network_prov_mgr_config_t config = {
        .scheme = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM 
    };
    network_prov_mgr_init(config);

    // 6. The Fork in the Road: Are we already provisioned?
    bool provisioned = false;
    // Note: The function name specifically added "_wifi_" in v6.0
    network_prov_mgr_is_wifi_provisioned(&provisioned);

    if (!provisioned) {
        ESP_LOGI(TAG, "No Wi-Fi credentials found. Starting BLE Provisioning...");
        
        // Start Provisioning. 
        network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1, PROOF_OF_POSSESSION, "PROV_ESP8684", NULL);
    } else {
        ESP_LOGI(TAG, "Credentials found in NVS. Skipping BLE and starting Wi-Fi...");
        
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
    }
}