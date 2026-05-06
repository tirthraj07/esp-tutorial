#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "mqtt_client.h" // <-- MQTT Client Library

// Constants
#define WIFI_SSID "yourSSID"
#define WIFI_PASSWORD "yourPassword"
#define MQTT_URL "mqtts://<placeholder>-ats.iot.us-east-1.amazonaws.com:8883"

// =======================================================
// IMPORT THE EMBEDDED AWS CERTIFICATES FROM CMAKE
// =======================================================
extern const uint8_t root_ca_pem_start[]   asm("_binary_root_ca_pem_start");
extern const uint8_t root_ca_pem_end[]     asm("_binary_root_ca_pem_end");
extern const uint8_t client_crt_start[]    asm("_binary_client_crt_start");
extern const uint8_t client_crt_end[]      asm("_binary_client_crt_end");
extern const uint8_t client_key_start[]    asm("_binary_client_key_start");
extern const uint8_t client_key_end[]      asm("_binary_client_key_end");

// Prototypes
void configure_wifi();
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void mqtt_app_start(void);
esp_mqtt_client_handle_t configure_mqtt(); 
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

// Main function starts here
void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    
    // Initialize the LwIP core (The Network Stack)
    esp_netif_init();
    // Create the default Event Loop (The Asynchronous Engine)
    esp_event_loop_create_default();
    // Create the default Wi-Fi Station network interface
    esp_netif_create_default_wifi_sta();

    // Configure Wifi
    configure_wifi();

    // Register Event Handlers
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    // Start Wi-Fi
    esp_wifi_start();
}


// Implementations

// The following function configures wifi
void configure_wifi(){
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
}


// The following function is the event handler that listens to any wifi related events
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        printf("Wi-Fi Disconnected. Retrying...\n");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        printf("Wi-Fi Connected! Spawning AWS MQTT Task...\n");
        mqtt_app_start(); 
    }
}


// The following function starts the secure mqtt client here - 
static void mqtt_app_start(void) {
    printf("\nInitializing AWS IoT Core MQTT Client...\n");
    
    // Configure MQTT Client
    esp_mqtt_client_handle_t client = configure_mqtt();

    // Register a MQTT Event Handler
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    
    // Start MQTT Client
    esp_mqtt_client_start(client);
}


// We configure the mqtt client here
esp_mqtt_client_handle_t configure_mqtt(){
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_URL,
        // Attach the passports we embedded via CMake
        .broker.verification.certificate = (const char *)root_ca_pem_start,
        .credentials.authentication.certificate = (const char *)client_crt_start,
        .credentials.authentication.key = (const char *)client_key_start,
    };
    
    // Initialize the client and return it
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    return client;
}

// We handle the MQTT Events in the following function
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            printf("\n>>> SUCCESS! AWS IoT Core mTLS Handshake Complete! <<<\n");
            // The moment we connect, subscribe to a testing topic!
            esp_mqtt_client_subscribe(client, "esp8684/testing", 0);
            
            // And publish a "Hello World" message to prove we are alive!
            esp_mqtt_client_publish(client, "esp8684/testing", "{\"status\": \"Hello I am Tirthraj!\"}", 0, 1, 0);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            printf("MQTT_EVENT_DISCONNECTED\n");
            break;
            
        case MQTT_EVENT_DATA:
            printf("\n--- Message Received from AWS ---\n");
            printf("TOPIC: %.*s\n", event->topic_len, event->topic);
            printf("DATA: %.*s\n", event->data_len, event->data);
            printf("---------------------------------\n");
            break;
            
        case MQTT_EVENT_ERROR:
            printf("MQTT_EVENT_ERROR: Something went wrong with the certificates or connection.\n");
            break;
            
        default:
            break;
    }
}