# HTTP GET Example (ESP-IDF) — Wi‑Fi + Blocking GET with Full JSON

This folder contains the `http_get` project. It connects the ESP32‑C2 (ESP8684-class chips) to your Wi‑Fi router, waits until the network stack has an IP address, then performs an HTTP GET and prints the full JSON response.

The goal of this example is to show **the “ESP-IDF way”** of doing networking:

- You **configure** the Wi‑Fi driver and network stack up front.
- You **register event handlers** (callbacks) that run asynchronously when Wi‑Fi/IP state changes.
- Your main logic can still be written in a clean, synchronous style by **waiting on a signal** (“we have an IP now”).

---

## Project layout

- `http_get/main/http_get.c`
  - Owns `app_main()`.
  - Starts Wi‑Fi, waits for IP, calls `handle_get()`, prints the returned JSON.
- `http_get/main/wifi_utils.c` + `wifi_utils.h`
  - Wi‑Fi init helper (`configure_wifi`).
  - Registers Wi‑Fi/IP event handlers.
  - Exposes `wifi_wait_for_ip()` to let `app_main()` block until DHCP completes.
- `http_get/main/http_utils.c` + `http_utils.h`
  - Implements `handle_get(url)` which performs a blocking GET and returns the full response body.

---

## High-level flow (what happens at runtime)

### 1) Initialize the persistence and networking services

In `app_main()` we do:

- `nvs_flash_init()` (erase + retry if needed)
  - Wi‑Fi uses NVS for calibration and persistent internal data.
- `esp_netif_init()`
  - Brings up ESP-IDF’s “glue” between drivers and the LwIP TCP/IP stack.
- `esp_event_loop_create_default()`
  - Creates the default event loop (the async engine that dispatches Wi‑Fi/IP events).
- `esp_netif_create_default_wifi_sta()`
  - Creates the default STA (station/client) interface.

### 2) Configure the Wi‑Fi driver

`configure_wifi(WIFI_SSID, WIFI_PASSWORD)`:

- Calls `esp_wifi_init()`
- Builds a `wifi_config_t` and copies SSID/password into the struct’s fixed-size buffers
- Calls:
  - `esp_wifi_set_mode(WIFI_MODE_STA)`
  - `esp_wifi_set_config(WIFI_IF_STA, &wifi_config)`

### 3) Register handlers before starting Wi‑Fi

`register_wifi_event_handlers()` registers one internal handler for:

- `WIFI_EVENT` (start/disconnect)
- `IP_EVENT_STA_GOT_IP` (DHCP success)

This ordering matters: **events can happen immediately after `esp_wifi_start()`**, so you want handlers registered first.

### 4) Start Wi‑Fi and wait for “IP ready”

`esp_wifi_start()` starts the Wi‑Fi driver; the connect attempt itself is asynchronous:

- The Wi‑Fi driver posts `WIFI_EVENT_STA_START`
- Our handler reacts by calling `esp_wifi_connect()`
- After association, DHCP runs and eventually the stack posts `IP_EVENT_STA_GOT_IP`

When we receive `IP_EVENT_STA_GOT_IP`, `wifi_utils.c` sets a bit in a FreeRTOS **event group**.

`app_main()` then calls:

- `wifi_wait_for_ip(portMAX_DELAY)`

which blocks (without busy-waiting) until that “has IP” bit is set.

---

## `handle_get(url)` — the “single call” abstraction

`handle_get()` (in `http_utils.c`) provides a simple interface:

- Input: a URL string
- Output:
  - HTTP status code
  - the full response body as a heap-allocated string

Conceptually it feels like:

1. “Do a GET”
2. “Give me the JSON as one string”

### But the response is still streamed under the hood

Even though `handle_get()` returns the full body at the end, ESP-IDF’s HTTP client delivers data in **chunks** through an **event callback** (`HTTP_EVENT_ON_DATA`).

So internally, `handle_get()`:

- Creates an `esp_http_client` instance
- Runs `esp_http_client_perform()` (blocking call)
- While the request is in progress, ESP-IDF repeatedly calls our event handler with chunks of the body
- We append those chunks into a growing buffer (`realloc`)
- After the transfer finishes, we return the final assembled string

This is an important pattern in ESP-IDF:

- **API looks synchronous** to the caller
- **Data delivery is event-driven** internally

### HTTPS and certificates

`handle_get()` attaches the built-in certificate bundle:

- `.crt_bundle_attach = esp_crt_bundle_attach`

That allows TLS validation without embedding a specific server certificate in the example.

---

## “Background work” and Core 0 (what runs where)

On ESP32‑C2, your application code runs on a single core (labeled CPU0 by ESP-IDF), but multiple FreeRTOS tasks exist:

- **Your `app_main()` task**: created by ESP-IDF and runs your top-level logic.
- **Wi‑Fi driver task**: runs internal Wi‑Fi state machine, RX/TX, etc.
- **TCP/IP (LwIP) task**: handles sockets, timers, and packet processing.
- **Event loop task**: dispatches `WIFI_EVENT` / `IP_EVENT` callbacks.

When you register handlers with `esp_event_handler_instance_register(...)`, your callback runs in the context of the event loop dispatch.

That is why the model is:

- `app_main()` does setup and waits
- asynchronous tasks do the work and notify you via events

---

## How to run

From `05-http-get/http_get`:

```bash
idf.py build flash monitor
```

Edit the credentials at the top of `main/http_get.c`:

- `WIFI_SSID`
- `WIFI_PASSWORD`

And set the URL:

- `API_URL`

---

## Expected output

After a successful Wi‑Fi connection and DHCP, you should see:

- `SUCCESS! Connected to Wi‑Fi! IP: ...`
- `HTTP GET Status = 200` (or similar)
- `Response:` followed by the JSON body

