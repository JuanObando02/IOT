// ============================================================
// Ejercicio Guiado 1: ESP32 como cliente MQTT
// Práctica Capa de Aplicación (MQTT) -- IoT USC (3)
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>


// ---- Credenciales de la red (modificar) ----
const char* WIFI_SSID = "PC-SEBASTIAN";
const char* WIFI_PASS = "Juan12345";

// ---- Grupo (modificar: grupo01, grupo02, ...) ----
#define GRUPO "GRUPO_08"

// ---- Broker MQTT público ----
const char*    MQTT_BROKER = "broker.hivemq.com";
const uint16_t MQTT_PUERTO = 1883;

// Topics: usc/iot/lab3/<grupo>/esp32/device001/<recurso>
const String TOPIC_BASE   = "usc/iot/lab3/" GRUPO "/esp32/device001/";
const String TOPIC_TEMP   = TOPIC_BASE + "temperatura";
const String TOPIC_HUM    = TOPIC_BASE + "humedad";
const String TOPIC_LUZ    = TOPIC_BASE + "luz";
const String TOPIC_ESTADO = TOPIC_BASE + "estado";
const String TOPIC_LED    = TOPIC_BASE + "led/control";

#define PERIODO_PUBLICACION  10000   // ms entre publicaciones
#define PERIODO_REINTENTO     5000   // ms entre reintentos de conexión

// Pines LED RGB y canales LEDC
#define PIN_LED_R  26
#define PIN_LED_G  27
#define PIN_LED_B  14
#define CH_R       0
#define CH_G       1
#define CH_B       2
#define PWM_FREQ   5000
#define PWM_BITS   8

// Pines sensores
#define PIN_LDR   34
#define PIN_DHT   33
#define DHT_TYPE  DHT22

DHT dht(PIN_DHT, DHT_TYPE);
WiFiClient clienteWiFi;            // conexión TCP hacia el broker
PubSubClient mqtt(clienteWiFi);    // cliente MQTT sobre esa conexión

String clientId;                   // único por placa: esp32-<grupo>-<MAC>
unsigned long ultimaPublicacion = 0;
unsigned long ultimoIntento     = 0;

// ---- Funciones auxiliares ----

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(CH_R, r);
  ledcWrite(CH_G, g);
  ledcWrite(CH_B, b);
}

// Conecta en modo estación (STA); devuelve false si no lo logra en 15 s
bool conectarWiFi() {
  Serial.printf("Conectando a \"%s\"", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - inicio > 15000) {
      Serial.println("\nERROR: tiempo de conexion agotado");
      return false;
    }
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nConectado!");
  Serial.printf("  IP:   %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
  return true;
}

// Serializa un documento JSON y lo publica (QoS 0, sin retain)
void publicarJSON(const String& topic, const JsonDocument& doc) {
  char buffer[64];
  size_t n = serializeJson(doc, buffer);
  bool ok = mqtt.publish(topic.c_str(), (const uint8_t*)buffer, n, false);
  Serial.printf("PUB %-50s %s %s\n", topic.c_str(), buffer, ok ? "" : "(FALLO)");
}

// Lee los sensores y publica cada variable en su propio topic
void publicarSensores() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int luz = map(analogRead(PIN_LDR), 0, 4095, 0, 100);

  if (isnan(t) || isnan(h)) {
    Serial.println("ERROR: lectura del DHT22 fallida, no se publica");
  } else {
    JsonDocument docT;
    docT["temperatura"] = serialized(String(t, 1));   // 1 decimal: 24.3
    publicarJSON(TOPIC_TEMP, docT);

    JsonDocument docH;
    docH["humedad"] = serialized(String(h, 1));
    publicarJSON(TOPIC_HUM, docH);
  }

  JsonDocument docL;
  docL["luz"] = luz;
  publicarJSON(TOPIC_LUZ, docL);

  mqtt.publish(TOPIC_ESTADO.c_str(), "online");
}

// Callback: se ejecuta dentro de mqtt.loop() al llegar un mensaje suscrito
// Formato esperado en led/control: {"r":255,"g":0,"b":128}
void alRecibirMensaje(char* topic, byte* payload, unsigned int longitud) {
  Serial.printf("SUB %s (%u bytes)\n", topic, longitud);
  if (!TOPIC_LED.equals(topic)) return;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, longitud);
  if (error) {
    Serial.printf("  JSON invalido: %s\n", error.c_str());
    return;
  }
  if (!doc["r"].is<int>() || !doc["g"].is<int>() || !doc["b"].is<int>()) {
    Serial.println("  Faltan los campos r, g, b");
    return;
  }
  int r = constrain(doc["r"].as<int>(), 0, 255);
  int g = constrain(doc["g"].as<int>(), 0, 255);
  int b = constrain(doc["b"].as<int>(), 0, 255);
  setColor(r, g, b);
  Serial.printf("  LED -> R:%d G:%d B:%d\n", r, g, b);
}

// Reconexión no bloqueante: un intento cada PERIODO_REINTENTO ms,
// sin detener loop() mientras el broker o la red no responden
void mantenerConexion() {
  if (ultimoIntento != 0 && millis() - ultimoIntento < PERIODO_REINTENTO) return;

  if (WiFi.status() != WL_CONNECTED) {
    ultimoIntento = millis();
    Serial.println("WiFi desconectado, reintentando...");
    WiFi.reconnect();
    return;
  }
  if (mqtt.connected()) return;

  ultimoIntento = millis();
  Serial.printf("Conectando a %s como %s... ", MQTT_BROKER, clientId.c_str());
  if (mqtt.connect(clientId.c_str())) {
    Serial.println("conectado");
    mqtt.subscribe(TOPIC_LED.c_str(), 1);
    mqtt.publish(TOPIC_ESTADO.c_str(), "online");
  } else {
    // -2: sin conexión TCP, 2: Client ID rechazado, 4/5: credenciales
    Serial.printf("fallo (estado %d)\n", mqtt.state());
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== Ejercicio 1: ESP32 cliente MQTT ===");

  ledcSetup(CH_R, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_G, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_B, PWM_FREQ, PWM_BITS);
  ledcAttachPin(PIN_LED_R, CH_R);
  ledcAttachPin(PIN_LED_G, CH_G);
  ledcAttachPin(PIN_LED_B, CH_B);
  dht.begin();

  WiFi.mode(WIFI_STA);
  if (!conectarWiFi()) {
    setColor(255, 0, 0);  // rojo fijo: sin red (loop() seguirá reintentando)
  }

  // Client ID único: dos clientes con el mismo ID se desconectan entre sí
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  clientId = String("esp32-") + GRUPO + "-" + mac.substring(6);

  mqtt.setServer(MQTT_BROKER, MQTT_PUERTO);
  mqtt.setCallback(alRecibirMensaje);
  Serial.printf("Topic base: %s\n", TOPIC_BASE.c_str());
}

void loop() {
  mantenerConexion();
  mqtt.loop();            // procesa mensajes entrantes y mantiene el keep-alive

  if (mqtt.connected() && millis() - ultimaPublicacion >= PERIODO_PUBLICACION) {
    ultimaPublicacion = millis();
    publicarSensores();
  }
}
