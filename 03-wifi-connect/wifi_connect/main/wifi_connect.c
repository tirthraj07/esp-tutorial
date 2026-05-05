#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// The Core Wi-Fi and Networking Libraries
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

/*
==========================================
THE CALLBACK (Event Handler)
This runs in the background whenever the Wi-Fi state changes.
==========================================

The Wi-Fi stack is deeply asynchronous. When you call esp_wifi_connect(), \
the connection doesn't happen instantly
it involves:
    Scanning for the SSID
    Sending authentication frames
    Waiting for the router's response
    DHCP negotiation
    ...all taking hundreds of milliseconds

You don't want your code to sit in a blocking loop waiting. 
You want to tell the system "go connect, and call me when something happens." 
This is the event loop pattern — identical conceptually to Node.js's event loop or JavaScript's addEventListener.

The ESP-IDF event loop is a FreeRTOS task that runs a message queue. 
When something happens (Wi-Fi connected, got IP, disconnected), 
the driver posts an event to this queue. 
The event loop task dequeues it and calls all registered handler functions.
*/
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    // 1. If the antenna successfully powers on...
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        printf("Wi-Fi Driver Started! Attempting to connect to Mahajan...\n");
        esp_wifi_connect(); // Send the command to connect
    } 
    // 2. If the router rejects us or we lose signal...
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        printf("Disconnected from router. Retrying...\n");
        esp_wifi_connect(); // Try again infinitely
    } 
    // 3. If the router accepts us and gives us an IP address!
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        printf("\n==========================================\n");
        printf("SUCCESS! Connected to Wi-Fi!\n");
        printf("IP Address: " IPSTR "\n", IP2STR(&event->ip_info.ip));
        printf("==========================================\n\n");
    }
}

void app_main(void) {
    /*
    ==========================================
    PHASE 1: INITIALIZATION
    ==========================================
    1. Initialize NVS (Non-Volatile Storage)
    NVS = Non-Volatile Storage. This is a key-value store that lives in a dedicated partition of 
    the ESP32's Flash memory. It survives power cycles.
    Why does Wi-Fi need it? The esp_wifi driver uses NVS to store RF calibration data — 
    precise measurements of the analog RF circuitry that were taken during chip manufacturing at Espressif's factory.
    
    Each chip's RF characteristics are slightly different due to manufacturing tolerances. 
    These calibration values compensate for those differences so the radio transmits at the 
    exact right frequency and power level. 
    Without NVS, the Wi-Fi driver would have to recalibrate from scratch every boot (adding ~100ms delay).
    
    NVS also stores Wi-Fi credentials if you use esp_wifi_set_storage(WIFI_STORAGE_FLASH), and Bluetooth pairing keys.
    */
    esp_err_t ret = nvs_flash_init();

    // If the NVS memory is corrupted or a new version is found, erase it and try again.
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }


    // 2. Initialize the LwIP core (The Network Stack)
    esp_netif_init();

    // 3. Create the default Event Loop (The Asynchronous Engine)
    esp_event_loop_create_default();

    /*
        esp_netif_init(); &  esp_event_loop_create_default();
        These two lines boot up the internal "operating system" services. 
        They start the background threads that will eventually manage your IP address 
        and listen for Wi-Fi events.
    */

    // 4. Create the default Wi-Fi Station network interface
    /*
        "STA" stands for Station. This tells the network stack that our chip will 
        act as a client connecting to a router, rather than acting as an Access Point (AP) 
        broadcasting its own Wi-Fi network.
    */
    esp_netif_create_default_wifi_sta();

    printf("Phase 1 Complete: Network Stack Initialized!\n");

    // ==========================================
    // PHASE 2: CONFIGURATION
    // ==========================================
    // Load the default factory settings for the Wi-Fi driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    
    /*
    esp_wifi_init() does the following
    1. Allocates memory for the Wi-Fi driver's internal state
    2. Starts the Wi-Fi driver task on Core 0
    3. Initialises the RF hardware through a series of register writes to the RF transceiver
    4. Loads the NVS calibration data into the RF hardware registers
    */
    esp_wifi_init(&cfg);

    // Create a strict C-struct holding your specific credentials
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "YourSSID",
            .password = "YourPass",
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, // Expect standard home security
        },
    };

    // Apply the settings: "Act as a Station (Client) and use these credentials"
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);


    // ==========================================
    // PHASE 3: REGISTRATION (The Event Loop)
    // ==========================================
    // "Listen to any WIFI_EVENT or IP_EVENT. If one happens, run 'wifi_event_handler'."
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    // ==========================================
    // PHASE 4: START
    // ==========================================
    printf("Configuration complete. Powering on the Wi-Fi antenna...\n");
    esp_wifi_start(); // This immediately triggers WIFI_EVENT_STA_START

    // Keep the main thread alive infinitely
    while (1) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}
