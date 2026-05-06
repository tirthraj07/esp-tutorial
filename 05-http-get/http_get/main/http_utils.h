#pragma once

#include "esp_err.h"

// Performs a blocking HTTP GET and returns the full response body as a heap buffer.
// The caller owns `*out_body` and must `free()` it.
esp_err_t handle_get(const char* url, int* out_status_code, char** out_body);

