#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Networking libraries
#include "esp_err.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "wifi_utils.h"
#include "http_utils.h"

#define WIFI_SSID "Mahajan"
#define WIFI_PASSWORD "aditya31"

#define API_URL "http://api.open-notify.org/iss-now.json"

void app_main(void) {
    // Initialize NVS and Network Stack
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    (void)esp_netif_create_default_wifi_sta();

    // Configure Wi-Fi (check wifi_utils.c)
    configure_wifi(WIFI_SSID, WIFI_PASSWORD);

    // Register Event Handlers (check wifi_utils.c)
    register_wifi_event_handlers();

    // Start Wifi
    ESP_ERROR_CHECK(esp_wifi_start());

    // Block until we have an IP address, then do the GET in a simple synchronous flow.
    if (!wifi_wait_for_ip(portMAX_DELAY)) {
        printf("Failed to get IP address\n");
        return;
    }

    int status_code = -1;
    char* json = NULL;
    esp_err_t err = handle_get(API_URL, &status_code, &json);
    if (err == ESP_OK) {
        printf("\nHTTP GET Status = %d\n", status_code);
        printf("Response:\n%s\n", json);
    } else {
        printf("\nHTTP GET request failed: %s\n", esp_err_to_name(err));
    }
    free(json);

    // Keep main task alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }

}
