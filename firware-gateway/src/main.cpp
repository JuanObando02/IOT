#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

String clientId;
unsigned long lastPublish = 0;
unsigned long counter = 0;

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("Conectando a WiFi '%s'", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWiFi OK, IP: %s\n", WiFi.localIP().toString().c_str());
}

void connectMqtt() {
  while (!mqtt.connected()) {
    connectWiFi();
    Serial.printf("Conectando al broker %s:%d ... ", MQTT_HOST, MQTT_PORT);
    // Last Will: si el ESP32 se cae, el broker publica "offline" (retenido)
    bool ok = mqtt.connect(clientId.c_str(), MQTT_USER[0] ? MQTT_USER : nullptr,
                           MQTT_USER[0] ? MQTT_PASS : nullptr, TOPIC_STATUS, 1, true, "offline");
    if (ok) {
      Serial.println("OK");
      mqtt.publish(TOPIC_STATUS, "online", true);
    } else {
      Serial.printf("fallo (rc=%d), reintento en 3s\n", mqtt.state());
      delay(3000);
    }
  }
}

void publishTelemetry() {
  JsonDocument doc;
  doc["id"] = clientId;
  doc["contador"] = ++counter;
  doc["uptime_s"] = millis() / 1000;
  doc["rssi"] = WiFi.RSSI();
  doc["temp_chip"] = temperatureRead();
  doc["heap_libre"] = ESP.getFreeHeap();

  char payload[256];
  size_t n = serializeJson(doc, payload, sizeof(payload));
  mqtt.publish(TOPIC_TELEMETRY, (const uint8_t*)payload, n, false);
  Serial.printf("[pub] %s -> %s\n", TOPIC_TELEMETRY, payload);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  clientId = "esp32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  connectWiFi();
}

void loop() {
  if (!mqtt.connected()) connectMqtt();
  mqtt.loop();

  if (millis() - lastPublish >= PUBLISH_INTERVAL_MS) {
    lastPublish = millis();
    publishTelemetry();
  }
}
