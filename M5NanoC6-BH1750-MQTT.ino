#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <BH1750.h>
#include <time.h>

#include "config.h"

// ===== Hardware =====
// M5NanoC6 HY2.0-4P (Grove) pin map:
// SDA = G2
// SCL = G1
static constexpr int I2C_SDA_PIN = 2;
static constexpr int I2C_SCL_PIN = 1;

// ===== MQTT =====
static constexpr const char* MQTT_TOPIC_LUX_RAW    = "home/env/lux/raw";
static constexpr const char* MQTT_TOPIC_LUX_META   = "home/env/lux/meta";
static constexpr const char* MQTT_TOPIC_LUX_STATUS = "home/env/lux/status";

static constexpr const char* MQTT_CLIENT_ID = "m5nanoc6_bh1750_lux";

// ===== Sensor =====
static constexpr uint8_t BH1750_ADDR = 0x23;
static constexpr uint32_t PUBLISH_INTERVAL_MS = 30000;

// ===== NTP =====
static constexpr const char* NTP_SERVER_1 = "ntp.nict.jp";
static constexpr const char* NTP_SERVER_2 = "pool.ntp.org";
static constexpr const char* TZ_INFO = "JST-9";
static constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 15000;

// ===== Meta / filtering =====
static constexpr size_t HISTORY_SIZE = 12;
static constexpr float INVALID_LUX = -1.0f;

// ===== Globals =====
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
BH1750 lightMeter(BH1750_ADDR);

uint32_t lastPublishMs = 0;
float luxHistory[HISTORY_SIZE] = {0};
size_t historyCount = 0;
size_t historyIndex = 0;

uint32_t sequenceNo = 0;
uint32_t sensorErrorCount = 0;
uint32_t mqttReconnectCount = 0;
uint32_t wifiReconnectCount = 0;

bool sensorReady = false;
bool statusDirty = true;
bool timeValid = false;

// ------------------------
// Time
// ------------------------
uint32_t getUnixTime() {
  time_t now = time(nullptr);
  if (now < 1700000000) {
    return 0;
  }
  return static_cast<uint32_t>(now);
}

bool syncNTP() {
  Serial.println("[NTP] Starting NTP sync");
  configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2);

  uint32_t start = millis();
  while (millis() - start < NTP_SYNC_TIMEOUT_MS) {
    time_t now = time(nullptr);
    if (now >= 1700000000) {
      timeValid = true;
      Serial.print("[NTP] Sync OK. Unix time: ");
      Serial.println(static_cast<unsigned long>(now));
      return true;
    }
    delay(250);
  }

  timeValid = false;
  Serial.println("[NTP] Sync failed");
  return false;
}

// ------------------------
// Utility
// ------------------------
void markStatusDirty() {
  statusDirty = true;
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  ++wifiReconnectCount;
  markStatusDirty();

  Serial.printf("[WiFi] Connecting to %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start > 20000) {
      Serial.println("\n[WiFi] Timeout. Retrying...");
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      start = millis();
    }
  }

  Serial.println();
  Serial.println("[WiFi] Connected");
  Serial.print("[WiFi] IP: ");
  Serial.println(WiFi.localIP());

  markStatusDirty();
}

bool connectMQTTWithLastWill() {
  char payload[256];
  snprintf(
      payload,
      sizeof(payload),
      "{\"status\":\"offline\",\"reason\":\"last_will\",\"wifi\":\"unknown\",\"ip\":\"0.0.0.0\","
      "\"sensor_ready\":false,\"sensor_error_count\":%lu,\"wifi_reconnect_count\":%lu,"
      "\"mqtt_reconnect_count\":%lu,\"uptime_s\":0,\"seq\":%lu,\"unix_time\":0,\"time_valid\":false}",
      static_cast<unsigned long>(sensorErrorCount),
      static_cast<unsigned long>(wifiReconnectCount),
      static_cast<unsigned long>(mqttReconnectCount),
      static_cast<unsigned long>(sequenceNo));

  return mqttClient.connect(
      MQTT_CLIENT_ID,
      nullptr,
      nullptr,
      MQTT_TOPIC_LUX_STATUS,
      1,
      true,
      payload);
}

void connectMQTT() {
  while (!mqttClient.connected()) {
    ++mqttReconnectCount;
    markStatusDirty();

    Serial.printf("[MQTT] Connecting to %s:%u ...\n", MQTT_BROKER, MQTT_PORT);
    if (connectMQTTWithLastWill()) {
      Serial.println("[MQTT] Connected");
      markStatusDirty();
      return;
    }

    Serial.print("[MQTT] Failed, rc=");
    Serial.print(mqttClient.state());
    Serial.println(" retry in 2 sec");
    delay(2000);
  }
}

float readLuxAverage(uint8_t samples = 3, uint16_t sampleDelayMs = 180) {
  float sum = 0.0f;
  uint8_t validCount = 0;

  for (uint8_t i = 0; i < samples; ++i) {
    float lux = lightMeter.readLightLevel();
    if (lux >= 0.0f && !isnan(lux)) {
      sum += lux;
      ++validCount;
    }
    delay(sampleDelayMs);
  }

  if (validCount == 0) {
    return INVALID_LUX;
  }

  return sum / validCount;
}

void addHistory(float lux) {
  luxHistory[historyIndex] = lux;
  historyIndex = (historyIndex + 1) % HISTORY_SIZE;

  if (historyCount < HISTORY_SIZE) {
    ++historyCount;
  }
}

float getMovingAverage() {
  if (historyCount == 0) {
    return INVALID_LUX;
  }

  float sum = 0.0f;
  for (size_t i = 0; i < historyCount; ++i) {
    sum += luxHistory[i];
  }
  return sum / static_cast<float>(historyCount);
}

float getPreviousLux() {
  if (historyCount < 2) {
    return INVALID_LUX;
  }

  size_t prevIndex = (historyIndex + HISTORY_SIZE - 2) % HISTORY_SIZE;
  return luxHistory[prevIndex];
}

float getDeltaLux(float currentLux, float movingAverage) {
  if (currentLux < 0.0f || movingAverage < 0.0f) {
    return INVALID_LUX;
  }
  return currentLux - movingAverage;
}

float getDeltaFromPrevious(float currentLux, float previousLux) {
  if (currentLux < 0.0f || previousLux < 0.0f) {
    return INVALID_LUX;
  }
  return currentLux - previousLux;
}

float getRatePercent(float currentLux, float movingAverage) {
  if (currentLux < 0.0f || movingAverage <= 0.0f) {
    return INVALID_LUX;
  }
  return ((currentLux - movingAverage) / movingAverage) * 100.0f;
}

const char* classifyTrend(float ratePercent) {
  if (ratePercent == INVALID_LUX) return "unknown";
  if (ratePercent <= -20.0f) return "falling_fast";
  if (ratePercent <= -8.0f)  return "falling";
  if (ratePercent >= 20.0f)  return "rising_fast";
  if (ratePercent >= 8.0f)   return "rising";
  return "stable";
}

bool publishLuxRaw(float lux, uint32_t unixTime) {
  char payload[96];
  snprintf(payload, sizeof(payload),
           "{\"lux\":%.1f,\"unix_time\":%lu,\"time_valid\":%s}",
           lux,
           static_cast<unsigned long>(unixTime),
           timeValid ? "true" : "false");

  bool ok = mqttClient.publish(MQTT_TOPIC_LUX_RAW, payload, true);

  Serial.print("[MQTT] Publish ");
  Serial.print(MQTT_TOPIC_LUX_RAW);
  Serial.print(" = ");
  Serial.print(payload);
  Serial.println(ok ? " [OK]" : " [FAIL]");

  return ok;
}

bool publishLuxMeta(float currentLux,
                    float movingAverage,
                    float deltaLux,
                    float deltaPrev,
                    float ratePercent,
                    uint32_t unixTime) {
  char payload[384];
  snprintf(
      payload,
      sizeof(payload),
      "{\"lux\":%.1f,\"avg\":%.1f,\"delta\":%.1f,\"delta_prev\":%.1f,"
      "\"rate_pct\":%.2f,\"trend\":\"%s\",\"samples\":%u,\"interval_ms\":%lu,"
      "\"seq\":%lu,\"unix_time\":%lu,\"time_valid\":%s}",
      currentLux,
      movingAverage,
      deltaLux,
      deltaPrev,
      ratePercent,
      classifyTrend(ratePercent),
      static_cast<unsigned>(historyCount),
      static_cast<unsigned long>(PUBLISH_INTERVAL_MS),
      static_cast<unsigned long>(sequenceNo),
      static_cast<unsigned long>(unixTime),
      timeValid ? "true" : "false");

  bool ok = mqttClient.publish(MQTT_TOPIC_LUX_META, payload, true);

  Serial.print("[MQTT] Publish ");
  Serial.print(MQTT_TOPIC_LUX_META);
  Serial.print(" = ");
  Serial.print(payload);
  Serial.println(ok ? " [OK]" : " [FAIL]");

  return ok;
}

bool publishLuxStatus(const char* reason) {
  if (!mqttClient.connected()) {
    return false;
  }

  uint32_t unixTime = getUnixTime();

  char ipBuf[32];
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u",
             WiFi.localIP()[0], WiFi.localIP()[1],
             WiFi.localIP()[2], WiFi.localIP()[3]);
  } else {
    snprintf(ipBuf, sizeof(ipBuf), "0.0.0.0");
  }

  char payload[512];
  snprintf(
      payload,
      sizeof(payload),
      "{\"status\":\"%s\",\"reason\":\"%s\",\"wifi\":\"%s\",\"ip\":\"%s\","
      "\"sensor_ready\":%s,\"sensor_error_count\":%lu,\"wifi_reconnect_count\":%lu,"
      "\"mqtt_reconnect_count\":%lu,\"uptime_s\":%lu,\"seq\":%lu,\"unix_time\":%lu,\"time_valid\":%s}",
      sensorReady ? "ok" : "degraded",
      reason,
      WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
      ipBuf,
      sensorReady ? "true" : "false",
      static_cast<unsigned long>(sensorErrorCount),
      static_cast<unsigned long>(wifiReconnectCount),
      static_cast<unsigned long>(mqttReconnectCount),
      static_cast<unsigned long>(millis() / 1000UL),
      static_cast<unsigned long>(sequenceNo),
      static_cast<unsigned long>(unixTime),
      timeValid ? "true" : "false");

  bool ok = mqttClient.publish(MQTT_TOPIC_LUX_STATUS, payload, true);

  Serial.print("[MQTT] Publish ");
  Serial.print(MQTT_TOPIC_LUX_STATUS);
  Serial.print(" = ");
  Serial.print(payload);
  Serial.println(ok ? " [OK]" : " [FAIL]");

  if (ok) {
    statusDirty = false;
  }

  return ok;
}

// ------------------------
// Setup / loop
// ------------------------
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("========================================");
  Serial.println("M5NanoC6 + BH1750 -> MQTT publisher");
  Serial.println("Topics:");
  Serial.println("  home/env/lux/raw");
  Serial.println("  home/env/lux/meta");
  Serial.println("  home/env/lux/status");
  Serial.println("Features: NTP, unix_time, Last Will");
  Serial.println("========================================");

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  sensorReady = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, BH1750_ADDR, &Wire);
  if (!sensorReady) {
    ++sensorErrorCount;
    Serial.println("[BH1750] Sensor init failed. Check wiring.");
  } else {
    Serial.println("[BH1750] Sensor initialized");
  }
  markStatusDirty();

  connectWiFi();
  syncNTP();

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  connectMQTT();

  publishLuxStatus("boot");

  lastPublishMs = millis() - PUBLISH_INTERVAL_MS;
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    syncNTP();
  }

  if (!mqttClient.connected()) {
    connectMQTT();
    publishLuxStatus("mqtt_reconnected");
  }

  mqttClient.loop();

  if (statusDirty) {
    publishLuxStatus("state_changed");
  }

  const uint32_t now = millis();
  if (now - lastPublishMs >= PUBLISH_INTERVAL_MS) {
    ++sequenceNo;

    if (!timeValid) {
      syncNTP();
      markStatusDirty();
    }

    if (!sensorReady) {
      sensorReady = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, BH1750_ADDR, &Wire);
      if (sensorReady) {
        markStatusDirty();
      }
    }

    float lux = readLuxAverage();

    if (lux < 0.0f) {
      ++sensorErrorCount;
      sensorReady = false;
      markStatusDirty();
      Serial.println("[BH1750] Read failed");
      publishLuxStatus("sensor_read_failed");
    } else {
      if (!sensorReady) {
        sensorReady = true;
        markStatusDirty();
      }

      addHistory(lux);

      float avg = getMovingAverage();
      float prev = getPreviousLux();
      float delta = getDeltaLux(lux, avg);
      float deltaPrev = getDeltaFromPrevious(lux, prev);
      float ratePct = getRatePercent(lux, avg);
      uint32_t unixTime = getUnixTime();

      publishLuxRaw(lux, unixTime);
      publishLuxMeta(lux, avg, delta, deltaPrev, ratePct, unixTime);

      const char* statusReason = "periodic";
      if (statusDirty) {
        statusReason = sensorReady ? "sensor_recovered" : "state_changed";
      }
      publishLuxStatus(statusReason);
    }

    lastPublishMs = now;
  }

  delay(10);
}
