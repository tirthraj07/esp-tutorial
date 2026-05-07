#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

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


// We are injecting NODE ID from CMake automatically during compile time
// Global buffers to hold our dynamically generated topic strings
char topic_telemetry[100];
char topic_ping[100];

// --- Device Shadow Topics ---
char topic_shadow_delta[150];       // AWS will only publish to this topic if a user/app changes the desired state of the LED, and that state does not match the chip's currently reported state.
char topic_shadow_update[150];      // Whenever the chip turns its physical LED on or off (or boots up), it publishes to this topic to tell AWS its true, physical state.

// Prototypes
void build_dynamic_mqtt_topics();
void initialize_gpio_pin();
void setup_wifi();
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void mqtt_app_start(void);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
void telemetry_background_task(void *pvParameters);
bool is_event_on_topic(esp_mqtt_event_handle_t event, char* topic);
void handle_mqtt_data_event(esp_mqtt_event_handle_t event);
void handle_custom_command(const char *command);
void handle_data_on_ping_topic(esp_mqtt_event_handle_t event);
void handle_shadow_delta(esp_mqtt_event_handle_t event);
void report_shadow_state(esp_mqtt_client_handle_t client, const char *led_state);

// Main function starts here
void app_main(void) {
    build_dynamic_mqtt_topics();
    initialize_gpio_pin();
    setup_wifi();
}

// Implementations

// This function dynamically builds mqtt topics based on NodeID
void build_dynamic_mqtt_topics(){
    // Note: NODE ID is injected from CMAKE at Compile Time
    sprintf(topic_telemetry, "nodes/%s/telemetry", NODE_ID);
    sprintf(topic_ping, "nodes/%s/ping", NODE_ID);

    // --- Build the Reserved AWS Shadow Topics ---
    // Device Shadows use AWS Reserved Topics. They all begin with $aws/things/.
    sprintf(topic_shadow_delta, "$aws/things/%s/shadow/update/delta", NODE_ID);
    sprintf(topic_shadow_update, "$aws/things/%s/shadow/update", NODE_ID);

    printf("Assigned Telemetry Topic: %s\n", topic_telemetry);
    printf("Assigned Ping Topic: %s\n", topic_ping);
    printf("Assigned Shadow Delta Topic: %s\n", topic_shadow_delta);
    printf("Assigned Shadow Update Topic: %s\n", topic_shadow_update);
}

// This function initializes the led pin
void initialize_gpio_pin(){
    gpio_reset_pin(BLINK_GPIO);
    // INPUT_OUTPUT keeps the input buffer enabled so gpio_get_level()
    // can read back the value we drive (needed for telemetry reporting).
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_INPUT_OUTPUT);
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
        // Explicitly set the Client ID to match our Thing Name!
        .credentials.client_id = NODE_ID,
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
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            printf("\n>>> SUCCESS! AWS IoT Core mTLS Handshake Complete! <<<\n");
            
            // Subscribe to our dynamic "ping" topic
            esp_mqtt_client_subscribe(client, topic_ping, 0);

            // --- Subscribe for Shadow State Changes ---
            esp_mqtt_client_subscribe(client, topic_shadow_delta, 0);

            // Spawn the Background Telemetry Task
            xTaskCreate(telemetry_background_task, "telemetry_task", 4096, (void *)client, 5, NULL);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            printf("MQTT_EVENT_DISCONNECTED\n");
            break;
            
        case MQTT_EVENT_DATA:
            handle_mqtt_data_event(event);
            break;
            
        case MQTT_EVENT_ERROR:
            printf("MQTT_EVENT_ERROR: Connection failed.\n");
            break;
            
        default:
            break;
    }
}

// This is a background telemetry task that publishes the free ram bytes and gpio level at some interval
void telemetry_background_task(void *pvParameters) {
    // We pass the MQTT client into this task when we create it
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)pvParameters;

    while (1) {
        // 1. Gather Data
        uint32_t free_ram = esp_get_free_heap_size();
        int current_led_state = gpio_get_level(BLINK_GPIO);

        // 2. Build the JSON Object
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "node_id", NODE_ID);
        cJSON_AddNumberToObject(root, "free_ram_bytes", free_ram);
        cJSON_AddStringToObject(root, "led_state", current_led_state == LED_LEVEL_ON ? "ON" : "OFF");

        char *json_string = cJSON_PrintUnformatted(root);

        // 3. Publish to our secure, dynamically generated topic!
        printf("\n[Telemetry Task] Publishing: %s\n", json_string);
        esp_mqtt_client_publish(client, topic_telemetry, json_string, 0, 1, 0);

        // 4. Cleanup memory
        cJSON_free(json_string);
        cJSON_Delete(root);

        // 5. Sleep for 10 seconds before sending again
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// Helper to check if the on data event is on the topic
bool is_event_on_topic(esp_mqtt_event_handle_t event, char* topic){
    return (event->topic_len == strlen(topic) && strncmp(event->topic, topic, event->topic_len) == 0);
}

// This function will handle all the mqtt data events
// We need to write the topic router in this function
void handle_mqtt_data_event(esp_mqtt_event_handle_t event){
    if (is_event_on_topic(event, topic_ping)){ 
        printf("Routing to: PING_HANDLER\n");
        handle_data_on_ping_topic(event);
    } else if (is_event_on_topic(event, topic_shadow_delta)) {
        // --- Route the Shadow Delta ---
        printf("Routing to: SHADOW_DELTA_HANDLER\n");
        handle_shadow_delta(event);
    }else {
        printf("Unknown Topic: %.*s\n", event->topic_len, event->topic);
    }
    printf("---------------------------------\n");
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


void handle_data_on_ping_topic(esp_mqtt_event_handle_t event) {
    cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
    // Handle parsing error
    if (root == NULL) {
        printf("Error parsing JSON data.\n");
    } else {
        // Look for the "message" key
        cJSON *msg = cJSON_GetObjectItem(root, "message");

        if (cJSON_IsString(msg) && (msg->valuestring != NULL)) {
            printf("Parsed Message: %s\n", msg->valuestring);
        }
        // Always delete the JSON object to prevent memory leaks!
        cJSON_Delete(root);
    }
}

// Parses the incoming Delta from AWS
void handle_shadow_delta(esp_mqtt_event_handle_t event) {
    cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
    if (root == NULL) {
        printf("Error parsing Shadow Delta JSON.\n");
        return;
    }

    // A Delta payload looks like: {"state": {"led": "ON"}}
    cJSON *state = cJSON_GetObjectItem(root, "state");
    if (state != NULL) {
        cJSON *led = cJSON_GetObjectItem(state, "led");
        
        if (cJSON_IsString(led) && (led->valuestring != NULL)) {
            printf("-> Shadow requests LED to be: %s\n", led->valuestring);
            
            // Actuate the hardware
            handle_custom_command(led->valuestring);

            // 2. Acknowledge the change back to AWS!
            report_shadow_state(event->client, led->valuestring);
        }
    }
    cJSON_Delete(root);
}

// Helper function: Tells AWS what the physical hardware is ACTUALLY doing
void report_shadow_state(esp_mqtt_client_handle_t client, const char *led_state) {
    // IMPORTANT NOTE :
    // We must build exactly: {"state": {"reported": {"led": "ON"}}}
    cJSON *root = cJSON_CreateObject();
    cJSON *state = cJSON_CreateObject();
    cJSON *reported = cJSON_CreateObject();

    cJSON_AddStringToObject(reported, "led", led_state);
    cJSON_AddItemToObject(state, "reported", reported);
    cJSON_AddItemToObject(root, "state", state);

    char *json_string = cJSON_PrintUnformatted(root);
    
    printf("-> Reporting Shadow State to AWS: %s\n", json_string);
    // Publish to the 'update' topic
    esp_mqtt_client_publish(client, topic_shadow_update, json_string, 0, 1, 0);

    cJSON_free(json_string);
    cJSON_Delete(root);
}
