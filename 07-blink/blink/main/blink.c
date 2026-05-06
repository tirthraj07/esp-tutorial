#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

// Onboard blue LED on ESP8684 / ESP32-C2 DevKit
#define BLINK_GPIO 8

void app_main(void) {
    printf("--- Starting Blink ---\n");

    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    int isOn = 0;

    while (1) {
        if (isOn) {
            printf("LED OFF\n");
            gpio_set_level(BLINK_GPIO, 0);
        } else {
            printf("LED ON\n");
            gpio_set_level(BLINK_GPIO, 1);
        }

        isOn = !isOn;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
