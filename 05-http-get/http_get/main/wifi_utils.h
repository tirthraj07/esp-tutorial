#pragma once

#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>

void configure_wifi(const char* ssid, const char* password);
void register_wifi_event_handlers(void);
bool wifi_wait_for_ip(TickType_t timeout_ticks);

