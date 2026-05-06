# AWS IoT Core + MQTT on the ESP8684

This tutorial connects an ESP8684 (ESP32-C2 class chip) to **AWS IoT Core** using the **MQTT** protocol over a secure TLS connection.

The goal is to understand:

1. What AWS IoT Core is and which problem it solves.
2. What MQTT is and why we use it instead of HTTP.
3. How devices authenticate to the cloud using X.509 certificates and mutual TLS.
4. How to set up the cloud side both manually (AWS Console) and automatically (AWS CDK).
5. How the ESP8684 firmware connects to Wi-Fi, embeds the certificates, and talks to AWS over MQTT.

---

## 1. What is AWS IoT Core?

### The problem it solves

Imagine you want to send temperature readings from your ESP32 to the cloud every few seconds, and also push commands back down to the device. Without a managed service, you would have to build and operate all of this yourself:

- A server that accepts millions of simultaneous TCP connections.
- TLS certificate management for every device.
- An authentication system that works without usernames or passwords on a microcontroller.
- A pub/sub message bus to route data between devices and applications.
- A database to store device state.
- A way to push commands down to devices even when they are behind NAT or firewalls.
- All of this at 99.99% uptime, globally.

That is months of infrastructure work before you write a single line of business logic.

**AWS IoT Core is AWS's answer.** It is a fully managed service that handles all of the above. You bring the device firmware and the application logic. AWS handles the wires.

---

## 2. What is MQTT?

Before AWS IoT Core makes sense, you need to understand MQTT, because it is the primary protocol IoT Core speaks.

**MQTT (Message Queuing Telemetry Transport)** is a lightweight publish/subscribe messaging protocol designed specifically for constrained devices and unreliable networks. It was invented in 1999 by IBM engineers for monitoring oil pipelines over satellite links.

### Publish / Subscribe in one paragraph

Devices and applications never talk to each other directly. They talk to a **broker** in the middle. A device can:

- **Publish** a message to a named **topic** (for example, `esp8684/testing`).
- **Subscribe** to a topic to receive every message published to it.

The broker (AWS IoT Core, in our case) routes messages from publishers to all current subscribers of that topic.

### Why not just use HTTP?

You already know HTTP. Your ESP32 could just `POST` sensor data to a server. But HTTP has serious problems for IoT.

| Problem             | HTTP                                                | MQTT                                              |
| ------------------- | --------------------------------------------------- | ------------------------------------------------- |
| Connection overhead | New TCP + TLS handshake every request (~2 KB, ~500 ms) | One persistent connection, reused forever        |
| Header bloat        | 200 to 800 bytes of headers per request             | 2-byte minimum header                             |
| Server push         | Impossible. The server cannot initiate              | Native. The broker pushes to the device           |
| Battery             | Constant reconnection drains battery                | Persistent connection. The broker buffers messages |
| Unreliable network  | Request fails if disconnected                       | QoS levels handle retries automatically           |

MQTT uses a single persistent TCP connection (over TLS for security). The device connects once and stays connected. The broker pushes messages to the device the moment they arrive, without the device having to poll.

---

## 3. AWS IoT Core, component by component

### 3.1 The Device Gateway — the front door

This is the MQTT broker that your ESP32 actually connects to. It runs at a unique endpoint per AWS account:

```
a1b2c3d4e5f6g7-ats.iot.us-east-1.amazonaws.com:8883
```

It handles:

- TLS 1.2 / 1.3 termination (decrypts your encrypted connection).
- Mutual TLS (mTLS) authentication. Both sides present certificates.
- Millions of simultaneous persistent connections.
- WebSocket support on port 443 for browser clients or firewalls that block 8883.

The device gateway is fully managed and auto-scales. You never configure a connection limit. AWS has published that IoT Core handles billions of messages per day.

### 3.2 Authentication and mTLS — how devices prove identity

This is the most important security concept in IoT. You **cannot** use a username and password on a microcontroller, because passwords can be extracted from flash. Instead, IoT Core uses **X.509 certificates with mutual TLS**.

In regular HTTPS, only the server presents a certificate (proving it is really Amazon). In **mutual TLS**, both sides present certificates:

- The server proves it is AWS IoT Core (using the Amazon Root CA).
- The device proves it is a legitimate registered device using its own private key plus certificate.

The flow for setting up a device is:

1. AWS generates a certificate and a private key pair (or you bring your own CA).
2. The certificate is registered in IoT Core and associated with a **Thing** (the cloud representation of a device).
3. An **IoT Policy** (similar to an IAM policy) is attached to the certificate. It controls which topics the device can publish to and subscribe from.
4. You flash the certificate and the private key onto the ESP32.
5. When the ESP32 connects, it presents its certificate. IoT Core validates it, looks up the attached policy, and enforces it.

In this tutorial, we use a permissive `iot:*` policy to keep things simple. In production you would scope it to specific topics and actions.

---

## 4. Project layout

```
06-aws-iot-core/
├── aws_iot/                  # ESP-IDF firmware project
│   ├── CMakeLists.txt
│   └── main/
│       ├── aws_iot.c         # The firmware
│       ├── CMakeLists.txt    # Embeds the certificates into the binary
│       ├── idf_component.yml # Pulls in the espressif/mqtt component
│       ├── root_ca.pem       # (you provide) Amazon Root CA
│       ├── client.crt        # (you provide) device certificate
│       └── client.key        # (you provide) device private key
└── infra/                    # AWS CDK project (Python)
    ├── app.py
    ├── cdk.json
    ├── Makefile              # `make setup`, `make deploy`, `make destroy`
    ├── requirements.txt
    ├── infra/
    │   └── infra_stack.py    # Defines the Thing and the IoT Policy
    └── scripts/
        ├── setup_certs.sh    # Creates and downloads device certificates
        └── cleanup.sh        # Detaches and deletes certificates before destroy
```

---

## 5. Phase 1 — Cloud Setup

You can do this **manually** in the AWS Console or **automatically** with AWS CDK.

### 5.1 Manual setup (AWS Console)

Open your browser, log in to AWS, search for **IoT Core**, and follow these steps.

#### Step 1 — Create the Thing

1. On the left sidebar, navigate to **Manage -> All devices -> Things**.
2. Click **Create things**.
3. Choose **Create single thing** and click **Next**.
4. Give your chip a name (for example, `ESP8684_Node_1`). Leave the rest as default and click **Next**.

#### Step 2 — Generate the certificates

1. Choose **Auto-generate a new certificate**. AWS will play the role of our certificate factory.
2. Click **Next**.

#### Step 3 — Attach the security policy

AWS will now ask you to attach a policy. You do not have one yet, so create it.

1. Click **Create policy** (it opens in a new tab).
2. Name it `ESP_Dev_Policy`.
3. In the **Policy Document** section, click the **JSON** button on the right.
4. Replace whatever is in the box with this permissive testing policy:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": "iot:*",
      "Resource": "*"
    }
  ]
}
```

5. Click **Create** at the bottom.
6. Close that tab, go back to the **Thing** creation tab, hit the refresh icon next to **Policies**, check the box next to `ESP_Dev_Policy`, and click **Create thing**.

#### Step 4 — Download the certificates (critical)

A pop-up will appear. **Do not close it before you download the files.** AWS will never show you the private key again.

Save these three files into a new folder on your computer (for example, `E:\Espressif\aws_certs`):

- **Device certificate** (ends in `.pem.crt`).
- **Private key file** (ends in `-private.pem.key`).
- **Amazon Root CA 1**. Click the link, copy the text, and save it as `root_ca.pem`.

#### Step 5 — Get your endpoint URL

1. On the bottom-left sidebar of the IoT Core dashboard, click **Settings**.
2. Copy your **Endpoint** URL. It looks like `a1b2c3d4e5f6g7-ats.iot.us-east-1.amazonaws.com`.

You will paste this into the firmware in the next phase.

#### Step 6 — Move the certificates into the firmware

Copy the three downloaded AWS files into `aws_iot/main/` and rename them exactly as below, so the C code variables match:

- Amazon Root CA -> `root_ca.pem`
- Device certificate -> `client.crt`
- Private key -> `client.key`

### 5.2 Automatic setup (AWS CDK)

If you have **AWS CDK**, **Python**, and **Node.js** installed, you can do the same setup with two commands.

```bash
cd 06-aws-iot-core/infra

make setup    # one-time: bootstraps your AWS account for CDK
make deploy   # creates the Thing + Policy, then downloads certs into aws_iot/main/
```

When you are done with the tutorial:

```bash
make destroy  # detaches and deletes certs, then tears down the stack
```

#### What `infra/infra/infra_stack.py` does

This file defines the cloud resources as code. It is the CDK equivalent of clicking through the AWS Console.

```1:48:06-aws-iot-core/infra/infra/infra_stack.py
from aws_cdk import (
    Stack,
    aws_iot as iot,
    CfnOutput
)
from constructs import Construct

class InfraStack(Stack):

    def __init__(self, scope: Construct, construct_id: str, **kwargs) -> None:
        super().__init__(scope, construct_id, **kwargs)

        # Step 1: We need to create a security policy
        policy_document = {
            "Version": "2012-10-17",
            "Statement": [
                {
                    "Effect": "Allow",
                    "Action": "iot:*",
                    "Resource": "*"
                }
            ]
        }

        # We tell AWS to create the policy using that dictionary
        esp_policy = iot.CfnPolicy(
            self,
            "EspCDKPolicy",
            policy_name="ESP_Dev_Policy_CDK",
            policy_document=policy_document
        )


        # Step 2: We need to create a "Thing" (device) in AWS IoT Core
        # This will act as a digital twin to our ESP Chip
        esp_thing = iot.CfnThing(
            self,
            "EspCDKThing",
            thing_name="ESP8684_Node_CDK"
        )
```

Step by step:

1. It builds the same permissive `iot:*` JSON policy you saw in the manual flow.
2. It calls `iot.CfnPolicy(...)` to register that policy in AWS as `ESP_Dev_Policy_CDK`.
3. It calls `iot.CfnThing(...)` to create a Thing called `ESP8684_Node_CDK`. The Thing is the digital twin of your ESP chip in the cloud.
4. The `CfnOutput` lines print the names of the created resources to the terminal once the deployment is done.

What CDK does **not** do here is create the certificate. CDK is great at static infrastructure, but generating and downloading a private key is a one-shot action. We do that in a shell script instead.

#### What `infra/scripts/setup_certs.sh` does

```1:39:06-aws-iot-core/infra/scripts/setup_certs.sh
#!/bin/bash

# This scripts downloads the AWS IoT Core certificates
# Then renames them and saves them in ../aws_iot/main/
# It assumes that the Policy and Thing are already created in AWS IoT Core
# If you don't have them, create it using CDK first (make deploy)


THING_NAME="ESP8684_Node_CDK"
POLICY_NAME="ESP_Dev_Policy_CDK"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CERT_DIR="${SCRIPT_DIR}/../../aws_iot/main"


echo "1. Generating Certificates from AWS IoT Core..."
CERT_ARN=$(aws iot create-keys-and-certificate \
    --set-as-active \
    --certificate-pem-outfile "$CERT_DIR/client.crt" \
    --public-key-outfile "$CERT_DIR/client.pub.key" \
    --private-key-outfile "$CERT_DIR/client.key" \
    --query 'certificateArn' --output text)

echo "2. Attaching Policy to Certificate..."
aws iot attach-policy \
    --policy-name $POLICY_NAME \
    --target $CERT_ARN

echo "3. Attaching Certificate to Thing..."
aws iot attach-thing-principal \
    --thing-name $THING_NAME \
    --principal $CERT_ARN

echo "4. Downloading Amazon Root CA..."
curl -s -o "$CERT_DIR/root_ca.pem" https://www.amazontrust.com/repository/AmazonRootCA1.pem
```

In plain English, the script does four things, in order:

1. **Generate certificates.** `aws iot create-keys-and-certificate` asks AWS to mint a brand new X.509 certificate plus a matching key pair. It writes them directly to `aws_iot/main/client.crt` and `aws_iot/main/client.key` and saves the certificate ARN.
2. **Attach the policy to the certificate.** This tells AWS, "this certificate is allowed to do whatever `ESP_Dev_Policy_CDK` permits." Without this, the device can connect but cannot publish or subscribe.
3. **Attach the certificate to the Thing.** This binds the certificate to the digital twin we created in CDK. Now the Thing has a verified identity.
4. **Download the Amazon Root CA.** This is the public certificate the ESP32 will use to verify that the server it is talking to is genuinely AWS.

After this script runs, the firmware folder has all three files it needs (`client.crt`, `client.key`, `root_ca.pem`).

The `cleanup.sh` script does the reverse before `cdk destroy`. AWS will not let you delete a Thing that still has a certificate attached, so the cleanup script detaches everything and deactivates the certificate first.

---

## 6. Phase 2 — Firmware

The firmware lives in `aws_iot/main/aws_iot.c`. It is intentionally short. The whole job is:

1. Connect to Wi-Fi.
2. Wait until we have an IP address.
3. Open a secure MQTT connection to AWS using our certificates.
4. Subscribe to a test topic and publish one hello message.

### 6.1 The constants

```18:20:06-aws-iot-core/aws_iot/main/aws_iot.c
#define WIFI_SSID "yourSSID"
#define WIFI_PASSWORD "yourPassword"
#define MQTT_URL "mqtts://<placeholder>.iot.us-east-1.amazonaws.com:8883"
```

- `WIFI_SSID` and `WIFI_PASSWORD` are your home Wi-Fi credentials.
- `MQTT_URL` is your unique AWS IoT Core endpoint from Phase 1, prefixed with `mqtts://` (MQTT over TLS) and the port `8883`.

### 6.2 How the certificates get into the binary

The ESP32 has no filesystem in this project, so we need a way to ship `root_ca.pem`, `client.crt`, and `client.key` along with the compiled firmware. ESP-IDF gives us a clean trick for this called `EMBED_TXTFILES`.

```1:3:06-aws-iot-core/aws_iot/main/CMakeLists.txt
idf_component_register(SRCS "aws_iot.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "root_ca.pem" "client.crt" "client.key")
```

`EMBED_TXTFILES` tells CMake, "take these files and bake their bytes into the final firmware as named symbols." For each file, the build system creates two symbols:

- `_binary_<filename>_start` — pointer to the first byte.
- `_binary_<filename>_end` — pointer to one byte past the last byte.

The C code then declares those symbols as `extern` so it can use them like normal C strings:

```25:30:06-aws-iot-core/aws_iot/main/aws_iot.c
extern const uint8_t root_ca_pem_start[]   asm("_binary_root_ca_pem_start");
extern const uint8_t root_ca_pem_end[]     asm("_binary_root_ca_pem_end");
extern const uint8_t client_crt_start[]    asm("_binary_client_crt_start");
extern const uint8_t client_crt_end[]      asm("_binary_client_crt_end");
extern const uint8_t client_key_start[]    asm("_binary_client_key_start");
extern const uint8_t client_key_end[]      asm("_binary_client_key_end");
```

The result: the certificates live inside the firmware image. There is no SD card, no filesystem mount, and no over-the-air download. Just a pointer the MQTT client can read directly.

> Important: `client.key` is the private key. Anyone who has the firmware binary effectively has the device identity. For production, look at ESP-IDF's secure boot and flash encryption features.

### 6.3 The top-level flow

`app_main()` is small. Its only job is to bring up the system in the right order and start Wi-Fi.

```40:64:06-aws-iot-core/aws_iot/main/aws_iot.c
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
```

Steps:

1. `nvs_flash_init()` — Wi-Fi calibration data lives in NVS. If the partition is broken, we erase and retry.
2. `esp_netif_init()` — initializes the LwIP TCP/IP stack.
3. `esp_event_loop_create_default()` — creates the default event loop. This is the asynchronous engine that will deliver Wi-Fi and IP events to our handlers.
4. `esp_netif_create_default_wifi_sta()` — creates a Wi-Fi station interface (we are a client, not an access point).
5. `configure_wifi()` — sets SSID and password.
6. `esp_event_handler_instance_register(...)` — registers our handler **before** starting Wi-Fi, because events can fire immediately after `esp_wifi_start()`.
7. `esp_wifi_start()` — kicks the whole machine into motion.

### 6.4 How Wi-Fi connects

`configure_wifi()` is just configuration:

```70:84:06-aws-iot-core/aws_iot/main/aws_iot.c
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
```

The actual connection happens **asynchronously**, driven by events. `wifi_event_handler` is the brain:

```88:99:06-aws-iot-core/aws_iot/main/aws_iot.c
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
```

Three events matter:

- `WIFI_EVENT_STA_START` fires after `esp_wifi_start()`. We respond by calling `esp_wifi_connect()`.
- `WIFI_EVENT_STA_DISCONNECTED` fires if we lose the link. We just retry.
- `IP_EVENT_STA_GOT_IP` fires when DHCP assigns us an IP. **Only now** do we have a working internet connection, so this is the right moment to start MQTT.

That last step is the bridge from the Wi-Fi layer to the application layer.

### 6.5 How the MQTT client is configured

`mqtt_app_start()` initializes the client, registers an event handler, and starts it.

```103:114:06-aws-iot-core/aws_iot/main/aws_iot.c
static void mqtt_app_start(void) {
    printf("\nInitializing AWS IoT Core MQTT Client...\n");
    
    // Configure MQTT Client
    esp_mqtt_client_handle_t client = configure_mqtt();

    // Register a MQTT Event Handler
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    
    // Start MQTT Client
    esp_mqtt_client_start(client);
}
```

`configure_mqtt()` is where the certificates we embedded earlier finally get used:

```118:130:06-aws-iot-core/aws_iot/main/aws_iot.c
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
```

This is the heart of mTLS:

- `broker.address.uri` — where to dial. The `mqtts://` scheme on port 8883 forces TLS.
- `broker.verification.certificate` — the Amazon Root CA. The device uses this to verify "you really are AWS".
- `credentials.authentication.certificate` — our device certificate. The server uses this to identify the device.
- `credentials.authentication.key` — our device private key. Used to prove ownership of the certificate.

When `esp_mqtt_client_start()` runs, the ESP-IDF MQTT component opens a TCP connection on port 8883, negotiates TLS, presents both certificates, and starts the MQTT-level handshake. If anything is wrong (wrong endpoint, missing policy, expired cert), AWS will reject the connection and you will see `MQTT_EVENT_ERROR`.

### 6.6 Subscribing and publishing

Once the broker accepts the connection, ESP-IDF fires `MQTT_EVENT_CONNECTED`. That is our signal to do real work.

```133:166:06-aws-iot-core/aws_iot/main/aws_iot.c
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
```

Walking through each case:

- **`MQTT_EVENT_CONNECTED`** — the mTLS handshake is complete and we are talking MQTT. We immediately:
  - **Subscribe** to `esp8684/testing` with QoS 0. After this, every message published to that topic will arrive at the device.
  - **Publish** a JSON payload to the same topic. Because we are subscribed to it, our own message comes back to us as well.

  The arguments to `esp_mqtt_client_publish` are: `client, topic, payload, payload_len, qos, retain`. A `payload_len` of `0` tells the library to use `strlen()` on the payload.

- **`MQTT_EVENT_DATA`** — a message arrived on a topic we subscribed to. The event struct gives us the topic and payload as length-prefixed buffers (they are not null-terminated, so we use `%.*s` to print them safely).

- **`MQTT_EVENT_DISCONNECTED`** — we lost the connection. ESP-IDF's MQTT client will reconnect automatically.

- **`MQTT_EVENT_ERROR`** — something went wrong. The most common causes are a bad endpoint URL, certificates that do not match the registered Thing, or a missing policy.

### 6.7 The MQTT component dependency

The MQTT client itself comes from a managed component, declared in `idf_component.yml`:

```17:17:06-aws-iot-core/aws_iot/main/idf_component.yml
  espressif/mqtt: '*'
```

When you run `idf.py build`, the IDF component manager downloads `espressif/mqtt` into `managed_components/` automatically. You do not need to copy or vendor it.

---

## 7. How to run

### 7.1 Build, flash, and monitor the firmware

From `06-aws-iot-core/aws_iot`:

```bash
idf.py build
idf.py -p <PORT> flash monitor
```

Replace `<PORT>` with your serial port (for example, `COM3` on Windows or `/dev/ttyUSB0` on Linux).

Before building, make sure you have:

- Edited `WIFI_SSID` and `WIFI_PASSWORD` in `aws_iot.c`.
- Edited `MQTT_URL` to match your AWS IoT Core endpoint.
- Placed `root_ca.pem`, `client.crt`, and `client.key` in `aws_iot/main/`.

### 7.2 Watch the messages from the AWS Console

1. Open AWS IoT Core in the console.
2. Go to **Test -> MQTT test client**.
3. Subscribe to the topic `esp8684/testing`.
4. Reset the ESP. You should see the `Hello I am Tirthraj!` JSON arrive in the console.
5. Publish a message from the console to `esp8684/testing`. You should see it appear in the serial monitor on the device.

### 7.3 Expected serial output

```
Wi-Fi Connected! Spawning AWS MQTT Task...

Initializing AWS IoT Core MQTT Client...

>>> SUCCESS! AWS IoT Core mTLS Handshake Complete! <<<

--- Message Received from AWS ---
TOPIC: esp8684/testing
DATA: {"status": "Hello I am Tirthraj!"}
---------------------------------
```

---

## 8. Troubleshooting

| Symptom                                              | Likely cause                                                                                  |
| ---------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| `MQTT_EVENT_ERROR` right after connecting            | Wrong endpoint URL, certificate not attached to the Thing, or policy not attached to the cert |
| Connection works but publish does nothing            | The IoT Policy denies `iot:Publish` on that topic                                             |
| Build fails with "cannot find `_binary_..._start`"   | Filenames in `EMBED_TXTFILES` do not match the actual files on disk                           |
| Wi-Fi never gets an IP                               | Wrong SSID or password, or the router is on 5 GHz only (ESP8684 supports 2.4 GHz)             |
| `make destroy` fails on the CDK stack                | Certificates are still attached. Run `make destroy` again, which calls `cleanup.sh` first     |

---

## 9. What to try next

- Tighten the IoT Policy so the device can only publish and subscribe to its own topics.
- Move the Wi-Fi credentials out of source code and into NVS or a config partition.
- Add an **AWS IoT Rule** to forward every message on `esp8684/testing` into DynamoDB or a Lambda function.
- Use **Device Shadow** to keep desired state and reported state in sync, instead of plain pub/sub.
