#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_mac.h"
#include "esp_flash.h"

void app_main(void)
{
    printf("\n==== ESP CHIP CONFIGURATION ====\n");
    
    // 1. Ask the chip for its physical configuration
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    // Fixed: Using %d because model and revision are numbers
    printf("Chip Model ID: %d\n", chip_info.model);
    printf("Silicon Revision: %d\n", chip_info.revision);
    printf("CPU Cores: %d\n", chip_info.cores);
    
    // Fixed: Printing the raw features bitmask as a hex number
    printf("Raw Features Mask: 0x%08x\n", (unsigned int)chip_info.features);
    
    // boolean check for Wi-Fi and Bluetooth
    printf("Human Features: %s%s\n",
        (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "Wi-Fi " : "",
        (chip_info.features & CHIP_FEATURE_BLE) ? "Bluetooth-LE" : "");
    
    printf("\n");

    // 2. Read Flash Size using the modern v5/v6 API
    uint32_t flash_size;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        printf("Flash Size: %" PRIu32 " bytes (%" PRIu32 " MB)\n", flash_size, flash_size / (1024 * 1024));
    } else {
        printf("Failed to get flash size\n");
    }
    printf("\n");

    // 3. Read the permanent identity (MAC Address) from the eFuse
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    printf("Factory MAC Address (eFuse): %02x:%02x:%02x:%02x:%02x:%02x\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    printf("================================\n\n");

    // Sleep so we don't spam the terminal
    while (1) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}