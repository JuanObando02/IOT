#pragma once

// --- WiFi ---
#define WIFI_SSID "USC-LIBRE-1"
#define WIFI_PASS "juan12345"

// --- Broker MQTT ---
#define MQTT_HOST "broker.hivemq.com"
#define MQTT_PORT 1883
#define MQTT_USER ""   // dejar vacio si el broker es anonimo
#define MQTT_PASS ""

// --- Topics ---
// Cambia "grupo1" por el nombre de tu grupo para no mezclar datos con otros alumnos
#define TOPIC_TELEMETRY "usc/iot2026b/grupo08/telemetri"
#define TOPIC_STATUS    "iot/clase/grupo1/gateway/estado"

#define PUBLISH_INTERVAL_MS 5000
