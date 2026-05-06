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
#include "mqtt_client.h" 

#include "driver/gpio.h"
#include "cJSON.h"

// Constants
#define WIFI_SSID "yourSSID"
#define WIFI_PASSWORD "yourPassword"
#define MQTT_URL "mqtts://<placeholder>-ats.iot.us-east-1.amazonaws.com:8883"
#define BLINK_GPIO 8
/* Many boards wire the user LED active-low: LOW = lit, HIGH = dark. */
#define LED_LEVEL_ON  0
#define LED_LEVEL_OFF 1

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
void initialize_gpio_pin();
void setup_wifi();
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void mqtt_app_start(void);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
void handle_mqtt_data_event(esp_mqtt_event_handle_t event);
void handle_custom_command(const char *command);
void handle_data_on_command_topic(esp_mqtt_event_handle_t event);
void handle_data_on_testing_topic(esp_mqtt_event_handle_t event);

// Main function starts here
void app_main(void) {
    initialize_gpio_pin();
    setup_wifi();
}


// Implementations

// This function initializes the led pin
void initialize_gpio_pin(){
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BLINK_GPIO, LED_LEVEL_OFF);
}

// This function configures the wifi
void setup_wifi(){
    // Initialize NVS
    nvs_flash_init();
    // Initialze LwIP Core
    esp_netif_init();
    // Create the default Event Loop (The Async Engine)
    esp_event_loop_create_default();
    // Create the default Wifi Station Network Interface
    esp_netif_create_default_wifi_sta();

    // Configure Wifi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    // Set wifi to use the station mode
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

    // Register event handler -> Callback wifi_event_handler()
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    // Start Wi-Fi
    esp_wifi_start();
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
        // When connected to WIFI -> Start the MQTT Task -> mqtt_app_start()
        mqtt_app_start(); 
    }
}

// The following function starts the secure mqtt client here - 
static void mqtt_app_start(void) {
    printf("\nInitializing AWS IoT Core MQTT Client...\n");
    
    // Configure MQTT Client
    esp_mqtt_client_config_t mqtt_cfg = {
        // Use the MQTT Url
        .broker.address.uri = MQTT_URL,
        // Attach the passports we embedded via CMake
        .broker.verification.certificate = (const char *)root_ca_pem_start,
        .credentials.authentication.certificate = (const char *)client_crt_start,
        .credentials.authentication.key = (const char *)client_key_start,
    };

    // Initialize the client
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);

    // Register a MQTT Event Handler -> mqtt_event_handler()
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    
    // Start MQTT Client
    esp_mqtt_client_start(client);
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
            esp_mqtt_client_subscribe(client, "esp8684/commands", 0);

            // And publish a "Hello World" message to prove we are alive!
            esp_mqtt_client_publish(client, "esp8684/testing", "{\"message\": \"Hello I am Tirthraj!\"}", 0, 1, 0);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            printf("MQTT_EVENT_DISCONNECTED\n");
            break;
            
        case MQTT_EVENT_DATA:
            printf("\n--- Message Received from AWS ---\n");
            handle_mqtt_data_event(event);
            break;
            
        case MQTT_EVENT_ERROR:
            printf("MQTT_EVENT_ERROR: Something went wrong with the certificates or connection.\n");
            break;
            
        default:
            break;
    }
}

// This function will handle all the mqtt data events
// We need to write the topic router in this function
void handle_mqtt_data_event(esp_mqtt_event_handle_t event){
    // Create a helper to check the topic name safely
    // We compare the length FIRST, then the content to ensure an exact match
    if (event->topic_len == strlen("esp8684/commands") && strncmp(event->topic, "esp8684/commands", event->topic_len) == 0) {
        printf("Routing to: COMMAND_HANDLER\n");
        handle_data_on_command_topic(event);
    } else if (event->topic_len == strlen("esp8684/testing") && strncmp(event->topic, "esp8684/testing", event->topic_len) == 0) {
        printf("Routing to: TESTING HANDLER\n");
        handle_data_on_testing_topic(event);
    } else {
        printf("Unknown Topic: %.*s\n", event->topic_len, event->topic);
    }
    printf("---------------------------------\n");
}

// This function will handle all the data on command topic
void handle_data_on_command_topic(esp_mqtt_event_handle_t event){
    cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);

    // Handle parsing error
    if (root == NULL) {
        printf("Error parsing JSON data.\n");
    } else {
        // Look for the "command" key
        cJSON *cmd = cJSON_GetObjectItem(root, "command");

        if (cJSON_IsString(cmd) && (cmd->valuestring != NULL)) {
            printf("Parsed Command: %s\n", cmd->valuestring);
            // Delegate the command to the command handler! Separation of Concern
            handle_custom_command(cmd->valuestring);
        }
        // Always delete the JSON object to prevent memory leaks!
        cJSON_Delete(root);

    }
}

// We handle commands here from mqtt
void handle_custom_command(const char *command){
    // Safety check: ensure the string actually exists
    if (command == NULL) {
        printf("-> Error: Received NULL command\n");
        return;
    }

    // Actuate the hardware based on the command
    if (strcmp(command, "ON") == 0) {
        gpio_set_level(BLINK_GPIO, LED_LEVEL_ON);
        printf("-> Physical LED turned ON\n");
    } 
    else if (strcmp(command, "OFF") == 0) {
        gpio_set_level(BLINK_GPIO, LED_LEVEL_OFF);
        printf("-> Physical LED turned OFF\n");
    }
    else {
        // Catch-all for typos or future commands we haven't built yet
        printf("-> Unknown command received: %s\n", command);
    }

}


// This function will handle all the data on testing topic
void handle_data_on_testing_topic(esp_mqtt_event_handle_t event){
    cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);

    // Handle parsing error
    if (root == NULL) {
        printf("Error parsing JSON data.\n");
    } else {
        // Look for the "message" key
        cJSON *cmd = cJSON_GetObjectItem(root, "message");

        if (cJSON_IsString(cmd) && (cmd->valuestring != NULL)) {
            printf("Parsed message: %s\n", cmd->valuestring);
        }
        // Always delete the JSON object to prevent memory leaks!
        cJSON_Delete(root);
    }
}
