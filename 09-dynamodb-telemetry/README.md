# DynamoDB Telemetry (AWS IoT Core + ESP-IDF)

This tutorial provisions an AWS IoT Core ingestion pipeline and an ESP-IDF firmware example that publishes telemetry over MQTT (mTLS). Telemetry is automatically routed into a DynamoDB table using an AWS IoT Core **Topic Rule** (IoT SQL).

## How to run

### Prerequisites

- **AWS account** with permissions for IoT Core, IAM, DynamoDB, and CloudFormation.
- **AWS CLI** configured (`aws configure`) for a region (this project uses an endpoint in `us-east-1` in the firmware).
- **AWS CDK** installed.
- **Python** available for CDK (see `infra/requirements.txt`).
- **ESP-IDF** installed and your environment set up (so `idf.py` works).

### 1) Deploy the cloud infrastructure

From the repository root:

```bash
cd infra
make setup
make deploy
ESP_NODE_ID="ESP_NODE_01" make onboard-thing
```

What you should see:

- A fleet policy named `ESP_Fleet_Policy`
- A DynamoDB table named `ESP_Telemetry_Data`
- An IoT Rule named `Route_ESP_Telemetry_To_Dynamo`
- A new IoT Thing named `ESP_NODE_01`
- Certificates downloaded/generated into `telemetry_task/main/`:
  - `root_ca.pem`
  - `client.crt`
  - `client.key`

### 2) Build + flash the firmware

```bash
cd ../telemetry_task
idf.py set-target esp32c2
ESP_NODE_ID="ESP_NODE_01" idf.py build flash monitor
```

Important:

- `ESP_NODE_ID` is used **at build time** to compile the firmware with the correct Thing identity.
- The firmware sets the MQTT **Client ID** to the same value as `ESP_NODE_ID`, which must match the Thing name for the policy to allow connections.

## What’s happening (in depth)

This project has three layers:

- **Security layer**: a fleet-wide IoT policy (`ESP_Fleet_Policy`)
- **Database layer**: a DynamoDB table (`ESP_Telemetry_Data`)
- **Routing layer**: an IoT Topic Rule with IoT SQL (`Route_ESP_Telemetry_To_Dynamo`)

It then “onboards” a specific device identity (“Thing”) and builds firmware that can only publish/subscribe to its own topics.

### 1) The policy (what it is, and what it allows)

An **AWS IoT policy** is an IAM-like policy document, but evaluated by **AWS IoT Core** for MQTT operations. It answers:

- Can this TLS-authenticated principal **connect** to the IoT endpoint?
- If connected, can it **publish** to a topic?
- Can it **subscribe** to a topic filter?
- Can it **receive** messages on topics?

In AWS IoT Core, policies typically attach to an **X.509 certificate**. That certificate is the “passport” the device presents during the mTLS handshake.

This project creates exactly one fleet policy named **`ESP_Fleet_Policy`**, and attaches it to each device certificate during onboarding.

#### How this policy enforces per-device isolation

The key idea is this variable used throughout the policy:

- `${iot:Connection.Thing.ThingName}`

This resolves at runtime to **the Thing name associated with the currently connected certificate** (via the Thing ↔ principal attachment).

That allows a single policy to scale to many devices while still locking each device into “its own lane”.

#### Allowed actions (and why they exist)

The policy has these statements:

- **`iot:Connect`**:
  - **What it means**: allows the MQTT connection handshake to complete for a given MQTT Client ID.
  - **Why it matters**: IoT Core treats the MQTT Client ID as part of the authorization decision.
  - **What this project enforces**: the device may only connect when the MQTT Client ID matches its Thing name:
    - Resource: `arn:aws:iot:*:*:client/${iot:Connection.Thing.ThingName}`

- **`iot:Publish`**:
  - **What it means**: allows sending messages to a topic.
  - **What this project enforces**: the device can only publish under its own namespace:
    - Topic pattern: `nodes/{ThingName}/*`
    - Resource: `arn:aws:iot:*:*:topic/nodes/${iot:Connection.Thing.ThingName}/*`

- **`iot:Subscribe`**:
  - **What it means**: allows the device to create a subscription using a topic **filter** (including wildcards).
  - **Why it’s separate**: subscribing is not the same as receiving; IoT Core evaluates subscribe permissions against a `topicfilter` ARN.
  - **What this project enforces**: the device can only subscribe to filters under its own namespace:
    - Resource: `arn:aws:iot:*:*:topicfilter/nodes/${iot:Connection.Thing.ThingName}/*`

- **`iot:Receive`**:
  - **What it means**: allows actually receiving messages that match a subscription.
  - **What this project enforces**: the device may only receive messages on topics in its own namespace:
    - Resource: `arn:aws:iot:*:*:topic/nodes/${iot:Connection.Thing.ThingName}/*`

Net result:

- Even if one device tries to publish as another device (different topic path), it is rejected by policy.
- Even if it tries to connect using a different Client ID, it is rejected by policy.

This is the core security model: **identity = Thing name**, and **authorization = only topics for that Thing name**.

### 2) The DynamoDB table (what it is and why this shape)

**DynamoDB** is a serverless NoSQL key-value/document store. For telemetry, you typically want:

- Very high write throughput with low operational overhead
- Efficient queries per device over time
- No need to manage partitions, indexes, or servers manually

This tutorial creates the table:

- **Name**: `ESP_Telemetry_Data`
- **Billing**: `PAY_PER_REQUEST` (on-demand; you pay per read/write, no capacity planning)
- **Primary key**:
  - **Partition key**: `node_id` (string)
  - **Sort key**: `timestamp` (number)

#### Why `node_id` + `timestamp` is a classic telemetry key design

In DynamoDB, the partition key determines the “group” of items, and the sort key orders items within that group.

With:

- `node_id` as partition key: all telemetry for one device ends up in the same logical partition group (queryable together).
- `timestamp` as sort key: telemetry becomes naturally ordered by time, enabling efficient range queries like:
  - “Give me the last 30 minutes”
  - “Give me items between t1 and t2”

This is the simplest, most predictable schema for “time series per device” in DynamoDB.

### 3) AWS IoT SQL and the IoT Rule (from first principles)

#### What “AWS IoT SQL” is

AWS IoT SQL is a small SQL-like language used by AWS IoT Core **Rules Engine** to:

- Select which messages should trigger a rule (by topic)
- Transform the message payload
- Add computed fields (like timestamps)
- Route the resulting data to AWS services (DynamoDB, Lambda, S3, Kinesis, etc.)

It is not “SQL for querying databases.” It is “SQL for filtering and transforming **streaming MQTT messages**”.

#### Why we use IoT SQL instead of firmware-to-DynamoDB directly

From first principles, the device’s job is to:

- Measure/collect data
- Publish messages securely and reliably

The cloud’s job is to:

- Validate and route data
- Transform/enrich data (timestamps, metadata)
- Fan-out to multiple storage/processing systems

If firmware wrote directly to DynamoDB, you would need to embed AWS credentials/SDK logic, handle retries, schema evolution, and network edge cases in the device. That increases attack surface and complexity.

IoT Core’s rule engine gives you:

- **Decoupling**: firmware only speaks MQTT; backend can change without reflashing devices.
- **Serverless routing**: no custom ingestion service required.
- **Transformations**: add fields (like cloud ingestion time) consistently.
- **Multi-destination fanout**: one message can go to multiple actions later (DynamoDB + Lambda, etc.).

#### Features you’re using here

This CDK stack creates a Topic Rule with:

- **SQL statement**:
  - `SELECT *, timestamp() AS timestamp FROM 'nodes/+/telemetry'`

Breakdown:

- **`FROM 'nodes/+/telemetry'`**:
  - This is the MQTT topic filter the rule listens to.
  - `+` is the MQTT single-level wildcard.
  - It matches:
    - `nodes/ESP_NODE_01/telemetry`
    - `nodes/ESP_NODE_02/telemetry`
    - and so on

- **`SELECT *`**:
  - Take all JSON fields from the published message payload.

- **`timestamp() AS timestamp`**:
  - Inject a new field named `timestamp`.
  - This is the **cloud ingestion time** (when IoT Core processed the message), not the device’s local clock.
  - Using ingestion time avoids problems with drifting device clocks and makes ordering consistent.

- **Action: DynamoDBv2 PutItem**:
  - The rule uses the DynamoDBv2 action to write an item into `ESP_Telemetry_Data`.
  - It uses an IAM role assumed by `iot.amazonaws.com` that has write access to the table.

#### Critical implication: the payload must include `node_id`

Because the DynamoDB table’s partition key is `node_id`, the rule needs that attribute present in the message (so it can write a valid primary key).

This firmware includes `node_id` in every telemetry JSON message:

- `node_id`: compiled-in device identity (`NODE_ID`)
- `free_ram_bytes`: an example numeric metric
- `led_state`: `ON` / `OFF`

And the IoT rule injects:

- `timestamp`: ingestion time (number)

So each DynamoDB item becomes “device + time + metrics”.

### 4) Onboarding a Thing and generating certificates

“Onboarding” in this tutorial means creating a secure identity in AWS IoT Core that maps to one physical device.

When you run:

```bash
ESP_NODE_ID="ESP_NODE_01" make onboard-thing
```

the script does (in order):

- **Create a Thing** named exactly `ESP_NODE_01`
  - Think of a Thing as an identity record representing a device.

- **Create an X.509 certificate and key pair** (mTLS client auth)
  - This yields:
    - `client.crt` (certificate)
    - `client.key` (private key)
    - (and `client.pub.key` for completeness)
  - These are saved into `telemetry_task/main/` so ESP-IDF can embed them into the firmware image.

- **Attach the fleet policy (`ESP_Fleet_Policy`) to the certificate**
  - This grants the certificate the right to connect/publish/subscribe/receive under the constrained topic namespace.

- **Attach the certificate to the Thing**
  - This creates the Thing ↔ principal association.
  - This association is what allows `${iot:Connection.Thing.ThingName}` to resolve during authorization.

- **Download Amazon Root CA** into `telemetry_task/main/root_ca.pem`
  - The device uses this to validate the server certificate when connecting to `mqtts://...:8883`.

### 5) Injecting `ESP_NODE_ID` into the firmware (so it knows which Thing it is)

The firmware needs a stable device identity so it can:

- Use it as MQTT **Client ID**
- Publish telemetry to `nodes/{node_id}/telemetry`
- Subscribe to command/control topics like `nodes/{node_id}/commands`

This tutorial injects the node id at **compile time** using the environment variable `ESP_NODE_ID`.

Mechanism:

- During the build, the CMake file reads `$ENV{ESP_NODE_ID}`.
- If set, it defines `NODE_ID="ESP_NODE_01"` as a compile definition.
- The C code uses `NODE_ID` to build topic strings and set `.credentials.client_id`.

This design keeps the firmware generic: the same source code can be built for many Things just by changing `ESP_NODE_ID` when building.

### 6) How the background telemetry task ends up in DynamoDB (step-by-step)

#### Device side (ESP32-C2)

Once Wi‑Fi is connected and MQTT connects successfully:

- The firmware starts a FreeRTOS background task that runs forever.
- Every 10 seconds it:
  - Reads metrics (free heap, current LED GPIO state)
  - Builds a small JSON document that includes `node_id`
  - Publishes it to:
    - `nodes/{NODE_ID}/telemetry`

Important security behaviors:

- The MQTT connection uses **mTLS**:
  - Root CA validates the server.
  - Client certificate + private key authenticate the device.
- The MQTT Client ID is set to `NODE_ID`.

#### AWS IoT Core side (Rules Engine)

When IoT Core receives a message on `nodes/ESP_NODE_01/telemetry`:

- **Authorization** is evaluated using `ESP_Fleet_Policy`:
  - Is this certificate allowed to publish to `nodes/${ThingName}/*`?
  - If the ThingName is `ESP_NODE_01`, publishing to `nodes/ESP_NODE_01/telemetry` is allowed.

- The **Topic Rule** evaluates:
  - Does the topic match `nodes/+/telemetry`?
  - Yes → the rule triggers.

- The **IoT SQL** transformation runs:
  - Copies all JSON fields (`SELECT *`)
  - Adds `timestamp` as ingestion time

- The **DynamoDBv2 action** runs:
  - Assumes the rule’s IAM role (`iot.amazonaws.com`)
  - Writes an item to `ESP_Telemetry_Data` with:
    - `node_id` (partition key)
    - `timestamp` (sort key)
    - plus all other telemetry fields as attributes

#### What you get in DynamoDB

Over time, for each `node_id`, you get a chronologically sorted stream of telemetry items. Querying becomes straightforward:

- “Show me the newest telemetry for `ESP_NODE_01`”
- “Show me telemetry between two times”
- “Alert if free RAM drops below a threshold” (later, by adding another rule action or downstream processing)

## Notes and gotchas

- **Hardcoded MQTT endpoint**: `telemetry_task/main/telemetry_task.c` currently hardcodes the AWS IoT endpoint and Wi‑Fi credentials. If you deploy into a different AWS account/region, you must update `MQTT_URL` accordingly.
- **Certificate files are embedded into firmware**: `telemetry_task/main/CMakeLists.txt` embeds `root_ca.pem`, `client.crt`, and `client.key`. Re-onboarding a Thing will overwrite these files.

