# M5NanoC6 + BH1750 MQTT Publisher v4

**Language:** English | [日本語](README.ja.md)

[Integration Hub](https://github.com/omiya-bonsai/m5papers3-weather-learning-system)

## Overview

This project uses **M5NanoC6** and an M5Stack ambient light sensor unit
based on **BH1750FVI-TR** to publish lux values, metadata, and device status to MQTT.

This revision adds:

- Last Will for `home/lux_status`
- NTP synchronization
- `unix_time`
- `time_valid`

Published topics:

```text
home/lux
home/lux_meta
home/lux_status
```

---

## Changes In This Revision

### 1. Added Last Will

If the device dies silently, the MQTT broker publishes retained `offline`
status to `home/lux_status`.

Example:

```json
{
  "status": "offline",
  "reason": "last_will",
  "wifi": "unknown",
  "ip": "0.0.0.0",
  "sensor_ready": false,
  "sensor_error_count": 0,
  "wifi_reconnect_count": 0,
  "mqtt_reconnect_count": 0,
  "uptime_s": 0,
  "seq": 0,
  "unix_time": 0,
  "time_valid": false
}
```

This makes it easier to distinguish between "the value stopped changing" and
"the device stopped running."

### 2. Added NTP

The device synchronizes time at boot and again after Wi-Fi reconnection.

Configured servers:

- `ntp.nict.jp`
- `pool.ntp.org`
- JST

### 3. Added `unix_time`

Published payloads now include Unix time.

### 4. Added `time_valid`

The device publishes whether the internal clock is valid.

---

## Topic Specification

### `home/lux`

Until v3, this topic used a plain numeric payload. It now uses JSON so that
time information can be included.

Example:

```json
{
  "lux": 1234.5,
  "unix_time": 1743476400,
  "time_valid": true
}
```

### `home/lux_meta`

Example:

```json
{
  "lux": 1234.5,
  "avg": 1180.2,
  "delta": 54.3,
  "delta_prev": -20.5,
  "rate_pct": 4.60,
  "trend": "stable",
  "samples": 12,
  "interval_ms": 30000,
  "seq": 42,
  "unix_time": 1743476400,
  "time_valid": true
}
```

### `home/lux_status`

Example:

```json
{
  "status": "ok",
  "reason": "boot",
  "wifi": "connected",
  "ip": "192.168.0.2",
  "sensor_ready": true,
  "sensor_error_count": 0,
  "wifi_reconnect_count": 1,
  "mqtt_reconnect_count": 1,
  "uptime_s": 8,
  "seq": 0,
  "unix_time": 1743476400,
  "time_valid": true
}
```

---

## Important Note

`home/lux` was intentionally changed to JSON.

Reason:

- the requirement was to include Unix time in the topic payload
- a plain numeric payload cannot carry timestamp metadata
- JSON is more extensible for downstream use on M5PaperS3

However, if an existing subscriber expects `home/lux` to contain only a raw
number, this is a breaking change. In that sense, this revision prioritizes
practicality over backward compatibility.

If compatibility must be preserved, a safer alternative would be:

- keep `home/lux` as a plain number
- put `unix_time` only in `home/lux_meta`

---

## Included Features

- `home/lux`
- `home/lux_meta`
- `home/lux_status`
- moving average
- delta from previous value
- delta from moving average
- rate of change
- trend classification
- sequence counter
- retained publish
- NTP synchronization
- Unix time
- `time_valid`
- Last Will offline notification
- sensor failure count
- Wi-Fi / MQTT reconnect counters

---

## Wiring

M5NanoC6 HY2.0-4P (Grove):

- SDA = G2
- SCL = G1

```cpp
Wire.begin(2, 1);
```

---

## Required Libraries

Install these from the Arduino IDE Library Manager:

- `PubSubClient`
- `BH1750`

---

## Configuration File

`config.h`:

```cpp
static constexpr const char* WIFI_SSID = "YOUR_WIFI_SSID";
static constexpr const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
static constexpr const char* MQTT_BROKER = "broker.local";
static constexpr uint16_t MQTT_PORT = 1883;
```

---

## Verification Command

```bash
mosquitto_sub -h broker.local -t "home/lux#" -v
```

---

## Things To Check Next

1. Confirm that `boot` is published to `home/lux_status` at startup.
2. Confirm that `unix_time` appears in `home/lux_status`.
3. Confirm that NTP re-sync runs after Wi-Fi recovery.
4. Confirm that Last Will publishes `offline` after forced reset or power loss.

---

## License

- Project license: [MIT](./LICENSE)
