# Wi-Fi Provisioning (ESP-IDF)

This lesson shows how to give a device its home Wi‑Fi credentials **without hardcoding SSID and password in firmware**. That mismatch—fine on a breadboard, risky in a sealed product—is often called the **first-mile problem** of consumer IoT. The standard fix is **Wi‑Fi provisioning**: the chip checks **non-volatile storage (NVS)** first; only if credentials are missing does it enter **provisioning mode** so the user can supply them securely.

The example firmware lives under [`wifi_provisioning/`](wifi_provisioning/). It uses **BLE provisioning** via Espressif’s **network provisioning** component (ESP-IDF v6 naming and APIs).

## Why provisioning exists

Hardcoded credentials break the moment SSIDs or passwords change, and they do not scale to manufacturing. Provisioned firmware instead:

1. **Reads NVS** on boot.
2. If credentials exist → connect as a normal Wi‑Fi **station**.
3. If not → run **provisioning** (BLE or SoftAP) until credentials are received and saved, then connect.

## Industry-standard provisioning modes

Your ESP‑C2/C3‑class silicon (tutorial target: **ESP8684**) integrates Wi‑Fi and can use common patterns products use everywhere.

### 1. BLE provisioning (modern, app-first)

Best when you ship or assume a **companion mobile app** (similar idea to Ring, Nest, Philips Hue-style setup).

Rough flow:

- **Boot and advertise:** device starts BLE and advertises a setup name (this project uses **`PROV_ESP8684`**).
- **Handshake:** the phone app finds the device and opens a secured session (see **Proof of Possession** below).
- **Transfer:** the app collects the user’s Wi‑Fi password and sends it to the chip over BLE—without forcing the phone off the home network during setup.
- **Save and switch:** firmware stores credentials in **NVS**, stops BLE radio to save power, and joins the home router as STA.

### 2. SoftAP + captive portal (“works everywhere”)

Best when **no app install** is acceptable or needed, or as a fallback.

Rough flow:

- **Boot as AP:** ESP creates its own SSID (e.g. device-specific setup name).
- **User joins that Wi‑Fi:** from phone or laptop settings.
- **Captive portal:** a small HTTP server on the ESP serves a setup page or redirect (like airport/hotel hotspots).
- **User picks SSID/password** (often SSID scanned by the ESP, password typed by user).
- **Save and teardown AP:** NVS stores credentials; ESP exits AP mode and joins the home router as STA.

This repository’s **included firmware** demonstrates **BLE** only; SoftAP is the alternative shape for the same “empty NVS vs provisioned” bootstrap.

## How ESP-IDF helps

You do not need to reimplement provisioning cryptography, BLE GATT framing, NVS layouts, or the full Wi‑Fi STA bring-up glue from scratch.

Espressif provides provisioning through the **managed component** [`espressif/network_provisioning`](https://components.espressif.com/) (wired in [`wifi_provisioning/main/idf_component.yml`](wifi_provisioning/main/idf_component.yml)). Older tutorials may say “Wi‑Fi Provisioning Manager”; ESP-IDF v6 uses **`network_prov_mgr_*`** and **`NETWORK_PROV_*` events**. It typically covers:

- Whether Wi‑Fi is already provisioned (NVS/consistency checks).
- Starting **BLE or SoftAP** scheme when nothing is saved.
- **Security** (including PoP below).
- Tearing provisioning down once credentials are committed and STA can run.

Espressif also publishes the **Espressif Provisioning** apps for iOS and Android so you can **test BLE provisioning** before writing your own branded app (production apps often reuse Espressif’s open‑source provisioning pieces inside their UX).

[`wifi_provisioning/sdkconfig.defaults`](wifi_provisioning/sdkconfig.defaults) ensures **protocomm security version 1** is enabled (`CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_1=y`) because this example uses **`NETWORK_PROV_SECURITY_1`** with a PoP string.

## Build and configure

### Prerequisites

- **ESP-IDF** environment (`idf.py` available), suitable for ESP8684 / your board.
- **[Espressif Provisioning app](https://www.espressif.com/en/support/download/apps)** installed on your phone (for BLE testing).

### 1. Project configuration (`menuconfig`)

BLE firmware is heavier than Wi‑Fi‑only snippets. Depending on chip and flash layout you may need to adjust partitioning or enable Bluetooth stacks explicitly.

From [`wifi_provisioning/`](wifi_provisioning/):

```bash
cd wifi_provisioning
idf.py set-target esp32c2    # adjust if your tutorial board uses another target
idf.py menuconfig
```

Suggested areas to verify (exact menu paths vary slightly by ESP-IDF version):

- **Bluetooth:** enable Bluetooth; prefer **NimBLE** as the BLE host where offered (lighter than alternatives for constrained parts).
- **Network / Wi‑Fi provisioning:** ensure provisioning support aligns with your IDF menus for the managed component build.
- **Partition table:** if the image is too large, choose a **factory app friendly** preset or define a **custom partition table CSV** large enough for the combined Wi‑Fi + BLE + provisioning stack.

Save and exit when done.

### 2. Build, flash, and monitor

```bash
cd wifi_provisioning
idf.py build flash monitor
```

On a **fresh NVS erase** / first-ever provision, logs should resemble:

- **`No Wi-Fi credentials found. Starting BLE Provisioning...`**

Then on the phone:

1. Open **Espressif Provisioning** → **Provision device** → **BLE** (grant Bluetooth permission when asked).
2. Select the advertiser named **`PROV_ESP8684`** (BLE device name configured in firmware).
3. When asked for **Proof of Possession**, enter **`12345678`** ([`wifi_provisioning/main/wifi_provisioning.c`](wifi_provisioning/main/wifi_provisioning.c)).
4. Choose your visible SSID and enter password; tap **Provision**.

Serial logs should show credential receipt, provisioning completion / BLE teardown, STA start, DHCP, then something like **`SUCCESS! Connected with IP:`** when **`IP_EVENT_STA_GOT_IP`** fires.

### 3. The “cold boot trusts NVS” test

Press **RST** after a successful provisioning run. On reboot, provisioning should **not** restart; you should see:

- **`Credentials found in NVS. Skipping BLE and starting Wi-Fi...`**

STA should associate quickly—credentials now live only in NVS, not your source tree.

---

## Understanding the firmware entry path

The “fork” in [`app_main()`](wifi_provisioning/main/wifi_provisioning.c) is intentionally small:

```c
network_prov_mgr_is_wifi_provisioned(&provisioned);
if (!provisioned) {
    network_prov_mgr_start_provisioning(/* ... */);
} else {
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
}
```

### FAQ: “There’s only `if` / `else`—why did provisioning run and then STA connect?”

The important detail for FreeRTOS-era ESP-IDF code: **`network_prov_mgr_start_provisioning()` is asynchronous.** It spins up background tasks to run BLE provisioning; it **does not** block `app_main()` until someone taps Provision on the phone.

So on first boot:

1. **`app_main`** sees `provisioned == false`, enters the **`if`** branch, and calls **`network_prov_mgr_start_provisioning`**.
2. **Main task can return shortly after** (“Returned from `app_main`” appears in logs); that line does **not** mean provisioning stopped—it means the provisioning manager’s **own tasks** keep running.

While that happens:

- **`NETWORK_PROV_*` events** (registered on `NETWORK_PROV_EVENT`) report progress—for example **`NETWORK_PROV_WIFI_CRED_RECV`** when SSID/password arrive.
- The manager’s internals save to **NVS** and coordinate bringing up **STA**.
- **`WIFI_EVENT_STA_START`** in the handler calls **`esp_wifi_connect()`**.
- **`IP_EVENT_STA_GOT_IP`** logs successful addressing.

So you **typically do not** hit the **`else`** in the **same boot** path that kicked off provisioning. The **`else`** is for **the next reboot** once NVS reflects an already-provisioned state.

---

## After provisioning: routers, passwords, moves (“second-mile problem”)

If the SSID/password in NVS is wrong anymore, the STA may endlessly retry (**`Disconnected. Retrying...`** in this example’s handler) unless you provide a deliberate **recovery path**.

### Physical factory reset pattern

Consumer devices usually expose:

- **`esp_wifi_restore()`** — clear saved Wi‑Fi STA config.
- **`network_prov_mgr_reset()`** — clear provisioning bookkeeping in NVS.
- **`esp_restart()`** — reboot so the next **`network_prov_mgr_is_wifi_provisioned(...)`** path goes back through **provision-not-done** bootstrap.

Typically this is gated on a **long-press BOOT** GPIO (hardware varies by board)—press N seconds → erase + reboot → provisioning mode again.

### Online “soft trigger” alternative

While the device is still reachable over IP or MQTT/cloud, firmware can expose a **“leave setup state / clear Wi‑Fi creds”** command (often via shadow or MQTT) that runs the **same trio** (`network_prov_mgr_reset`, Wi‑Fi restore, reboot).

This repository does **not** implement the GPIO or cloud trigger; those are outlined here as production patterns layered on top of NVS‑first boot flows.

## File map

| Path | Role |
|------|------|
| [`wifi_provisioning/main/wifi_provisioning.c`](wifi_provisioning/main/wifi_provisioning.c) | NVS init, STA netif/event loop, provisioning manager setup, **`if`/ `else`** provision vs STA‑only startup, consolidated event logger |
| [`wifi_provisioning/main/idf_component.yml`](wifi_provisioning/main/idf_component.yml) | Declares **`espressif/network_provisioning`** dependency |
| [`wifi_provisioning/sdkconfig.defaults`](wifi_provisioning/sdkconfig.defaults) | Enables protocomm **security v1** for PoP + `NETWORK_PROV_SECURITY_1` |
