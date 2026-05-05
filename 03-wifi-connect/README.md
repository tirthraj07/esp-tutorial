# Wi-Fi Station Example: How This Program Connects to Your Router

This document explains, from first principles, what happens when you run the `wifi_connect` example on an Espressif chip (for example ESP8684 / ESP32-C series). It ties the C code in `main/wifi_connect.c` to the networking stack and the asynchronous model ESP-IDF uses.

If you are used to writing network code on a PC or server, much of this will feel unfamiliar at first. On a microcontroller there is no “full” Windows or Linux kernel doing the heavy lifting for you in the same way. Instead, you initialize a small set of services (network stack, event system, Wi-Fi driver), configure credentials, register callbacks, and then start the radio. Progress is reported back to you through **events**, not by blocking in a `while` loop until “connected.”

---

## 1. The network stack (LwIP)

When you call `fetch()` in Node.js or use `requests` in Python, the operating system’s TCP/IP stack turns that into packets, manages sockets, routing, and often DHCP for you. You rarely touch raw Ethernet or Wi-Fi frames.

The ESP8684 (and similar chips) does not run a desktop OS. ESP-IDF bundles **FreeRTOS** for tasks and scheduling, and **LwIP** (Lightweight IP) as the TCP/IP stack that sits above the Wi-Fi driver.

**What LwIP does (simplified):**

- **IP layer:** Assigns and uses IPv4 addresses on interfaces.
- **DHCP client:** After Wi-Fi associates with the access point, the station typically asks the router for an address via DHCP; LwIP participates in that process together with ESP-IDF’s integration code.
- **TCP / UDP:** Once you have an IP, LwIP can carry TCP and UDP traffic (this example stops once it has an IP; it does not open a socket).

**Our job in this example:** Before we can meaningfully use IP or DHCP, we must bring up the pieces ESP-IDF expects:

- `esp_netif_init()` — initializes the netif (network interface) layer that connects LwIP to drivers.
- `esp_netif_create_default_wifi_sta()` — creates the default **STA** (station / client) interface so the stack knows this device is a **client** joining an existing access point, not an AP broadcasting its own network.

Until these run, there is no proper path from “Wi-Fi driver got a link” to “LwIP has an IP configuration.”

---

## 2. The event loop (asynchronous C)

Connecting to Wi-Fi is slow in human terms: scanning, authenticating (WPA2, etc.), associating, then DHCP. If `app_main` blocked in a tight loop until “done,” you would tie up the CPU and starve other work (other tasks, the TCP/IP stack, housekeeping).

ESP-IDF uses a **default event loop** (and FreeRTOS tasks under the hood). You **register handlers** for event bases such as `WIFI_EVENT` and `IP_EVENT`. When the Wi-Fi driver or the network stack has something to report, it posts an event; your handler runs in that event context.

This is analogous in spirit to JavaScript’s event loop: you do not poll in a busy loop; you react to **“something happened”** messages.

In this project, `wifi_event_handler` is that callback. It runs when state changes—for example when the station interface starts, when the link drops, or when the stack receives an IP.

---

## 3. Why NVS appears first (`nvs_flash.h`)

`nvs_flash_init()` prepares **Non-Volatile Storage** in flash. The Wi-Fi subsystem uses NVS for calibration and other persistent data so that future connections can be more reliable and faster.

The pattern in the code:

1. Try `nvs_flash_init()`.
2. If the partition is in a bad state (`ESP_ERR_NVS_NO_FREE_PAGES`) or the NVS layout changed (`ESP_ERR_NVS_NEW_VERSION_FOUND`), erase and initialize again.

That is not “Wi-Fi configuration storage” for your SSID/password in this snippet—those are set in RAM via `wifi_config_t`. NVS here is mainly supporting the driver’s expectation that flash-backed storage exists.

---

## 4. `esp_err_t`

Most ESP-IDF APIs return `esp_err_t`. `ESP_OK` means success; other codes indicate specific failures. Checking return values in production code is good practice; this tutorial example focuses on the happy path and uses the result mainly for the NVS recovery branch.

---

## 5. The four phases in this program

The structure of `app_main` matches four logical phases. They mirror how you should think about bringing Wi-Fi up on ESP-IDF.

### Phase 1: Initialization

1. **NVS:** `nvs_flash_init()` (with erase/retry if needed).
2. **Netif / LwIP glue:** `esp_netif_init()`.
3. **Event loop:** `esp_event_loop_create_default()`.
4. **Default STA interface:** `esp_netif_create_default_wifi_sta()`.

Together, these start the services that will later carry IP events and interface configuration for a station.

### Phase 2: Configuration

1. **`esp_wifi_init()`** — initializes the Wi-Fi driver with default parameters (`WIFI_INIT_CONFIG_DEFAULT()`).
2. **`wifi_config_t`** — holds SSID, password, and options (here, `WIFI_AUTH_WPA2_PSK` as the expected auth mode for a typical home router).
3. **`esp_wifi_set_mode(WIFI_MODE_STA)`** — station (client) mode only.
4. **`esp_wifi_set_config(WIFI_IF_STA, &wifi_config)`** — applies the credentials to the station interface.

At this point the radio is not necessarily on yet; you have loaded **what** to connect to, not started scanning/associating.

### Phase 3: Registration (handlers on the event loop)

- `esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, ...)` — any Wi-Fi event goes to `wifi_event_handler`.
- `esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ...)` — specifically when the station gets an IPv4 address, the same handler runs.

So one function handles multiple event types; it distinguishes them with `event_base` and `event_id`.

### Phase 4: Start

- **`esp_wifi_start()`** powers up the Wi-Fi subsystem according to the current mode and config.

This leads to **`WIFI_EVENT_STA_START`**: the station interface has started. In this example, the handler reacts by calling **`esp_wifi_connect()`**, which begins association with the AP using the SSID/password you configured.

---

## 6. What the event handler does (end-to-end flow)

Rough sequence for a successful connection:

1. **`esp_wifi_start()`** → **`WIFI_EVENT_STA_START`**  
   Handler prints a message and calls **`esp_wifi_connect()`**.

2. The driver talks to your router over the air (802.11 authentication and association, then WPA2 handshake if applicable). This is outside your application code; it is firmware inside the Wi-Fi binary blob and hardware.

3. Once associated, the **IP stack** can run **DHCP** on the STA interface. When a usable IPv4 address is assigned, ESP-IDF posts **`IP_EVENT_STA_GOT_IP`**.

4. The handler casts `event_data` to `ip_event_got_ip_t*` and prints the IP (using `IPSTR` / `IP2STR`).

If the link drops, **`WIFI_EVENT_STA_DISCONNECTED`** may fire; this example prints a message and calls **`esp_wifi_connect()`** again for a simple retry loop.

So: **start** → **connect** → **(optional disconnect/retry)** → **got IP**. Your `app_main` does not wait for that sequence; it falls through to a long `vTaskDelay` loop only to keep the task alive so the program does not exit.

---

## 7. How this maps to the “network stack” picture

Conceptually, from bottom to top:

| Layer (conceptual) | Role in this example |
|--------------------|----------------------|
| Radio / 802.11 MAC | Handled inside Espressif Wi-Fi firmware; you configure mode, SSID, password. |
| Wi-Fi driver API | `esp_wifi_*` — init, set config, start, connect. |
| Netif + event system | `esp_netif_*`, `esp_event_*` — ties Wi-Fi link state to LwIP and your callbacks. |
| LwIP (TCP/IP) | DHCP client, IP configuration; emits `IP_EVENT_STA_GOT_IP` when ready. |
| Your application | Registers handlers, calls `esp_wifi_start` / `esp_wifi_connect`, reacts to events. |

You do not send raw DHCP packets by hand; ESP-IDF and LwIP coordinate that after the Wi-Fi link is up.

---

## 8. Security note about credentials

The example embeds SSID and password in source for clarity. For anything beyond local learning, prefer **build-time configuration** (for example Kconfig / `sdkconfig`) or **provisioning**, and avoid committing real passwords to git.

---

## 9. Build and flash (short)

From this project directory (with ESP-IDF environment set up):

```bash
idf.py set-target esp32c2
idf.py build
idf.py -p PORT flash monitor
```

Replace `esp32c2` and `PORT` with your chip and serial port. If your board uses a different target, use the matching `set-target`.

---

## 10. Files in this example

- `main/wifi_connect.c` — full flow: NVS, netif, events, Wi-Fi config, handler, `esp_wifi_start`.
- `main/CMakeLists.txt` — registers the main component source file.
- Top-level `CMakeLists.txt` — standard ESP-IDF project entry.

---

## Summary

- **LwIP** provides IP, DHCP, and TCP/UDP; **esp_netif** connects it to the Wi-Fi driver.
- The **event loop** lets Wi-Fi and IP progress happen in the background while your code registers **callbacks** instead of blocking.
- **Four phases:** initialize services → configure Wi-Fi → register event handlers → `esp_wifi_start()` (which triggers connect logic in the handler).
- **NVS** is initialized first so the Wi-Fi driver can use flash-backed data as required by ESP-IDF.

Reading `main/wifi_connect.c` alongside this README, you can follow each line to one of these ideas and see how a small amount of setup code turns into a live association and an IP address on your LAN.
