// ============================================================
// Ejercicio Adicional 2: GATEWAY ESP-NOW <-> MQTT (grupo B)
// Práctica Capa de Aplicación (MQTT) -- IoT USC
// ============================================================
// Subida: nodo --ESP-NOW--> gateway --MQTT--> .../nodos/<n>/telemetria
// Bajada: .../nodos/<n>/led/control --MQTT--> gateway --ESP-NOW--> LED del nodo
// ACK de capa MAC del envío ESP-NOW --> .../nodos/<n>/led/ack
// Requiere arduino-esp32 2.x (platform = espressif32@6.9.0).
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ---- Credenciales de la red (modificar). Solo 2.4 GHz, WPA2-Personal ----
const char* WIFI_SSID = "PC-SEBASTIAN";
const char* WIFI_PASS = "Juan12345";

// ---- Grupo del gateway (grupo B) ----
#define GRUPO "GRUPO_08"

// ---- Broker MQTT público ----
const char*    MQTT_BROKER = "broker.hivemq.com";
const uint16_t MQTT_PUERTO = 1883;

// Topics: usc/iot/lab3/<grupoB>/gateway/...
const String TOPIC_BASE   = "usc/iot/lab3/" GRUPO "/gateway/";
const String TOPIC_ESTADO = TOPIC_BASE + "estado";
const String TOPIC_NODOS  = TOPIC_BASE + "nodos/";     // + <n>/telemetria, <n>/led/control, <n>/led/ack

#define KEEPALIVE_S         15
#define PERIODO_REINTENTO   5000    // ms entre reintentos de conexión
#define PERIODO_ESTADO      10000   // ms entre líneas de estado en el Monitor Serie
#define MAX_GRUPOS          20      // ESP-NOW admite hasta 20 peers

// ---- Estructuras ----
// Mensaje: IDÉNTICA a la del nodo (práctica de capa de red)
typedef struct __attribute__((packed)) {
  uint8_t  grupo;
  uint32_t seq;          // número de secuencia para detectar pérdidas
  float    temperatura;
  float    humedad;
  uint16_t luz;          // porcentaje 0-100
} Mensaje;

// Comando de color que el gateway envía al nodo (3 bytes)
typedef struct __attribute__((packed)) { uint8_t r, g, b; } ComandoLed;

// Elementos de las colas FreeRTOS (de la tarea WiFi hacia loop())
typedef struct { uint8_t mac[6]; Mensaje m; } Recepcion;
typedef struct { uint8_t mac[6]; bool ok; } ResultadoEnvio;

QueueHandle_t colaRx;    // tramas recibidas de los nodos
QueueHandle_t colaAck;   // resultados del callback de envío

// Estado por nodo (índice = número de grupo). Solo se toca desde loop().
uint8_t  macNodo[MAX_GRUPOS + 1][6];   // MAC aprendida de sus tramas
bool     nodoActivo[MAX_GRUPOS + 1];
uint32_t ultimoSeq[MAX_GRUPOS + 1];
uint32_t perdidos[MAX_GRUPOS + 1];

WiFiClient clienteWiFi;
PubSubClient mqtt(clienteWiFi);

String clientId;
unsigned long ultimoIntento = 0;
unsigned long ultimoEstado  = 0;

// ---- ESP-NOW: callbacks (corren en la tarea del WiFi) ----
// Regla: no llamar a mqtt.publish() ni Serial aquí; solo copiar a una cola.

void onRecibido(const uint8_t* mac, const uint8_t* datos, int len) {
  if (len != (int)sizeof(Mensaje)) return;
  Recepcion r;
  memcpy(r.mac, mac, 6);
  memcpy(&r.m, datos, sizeof(Mensaje));
  xQueueSend(colaRx, &r, 0);
}

// Informa si el nodo confirmó la trama (ACK de capa MAC, no de aplicación)
void onEnviado(const uint8_t* mac, esp_now_send_status_t status) {
  ResultadoEnvio e;
  memcpy(e.mac, mac, 6);
  e.ok = (status == ESP_NOW_SEND_SUCCESS);
  xQueueSend(colaAck, &e, 0);
}

// ---- Funciones auxiliares ----

String macStr(const uint8_t* m) {
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(b);
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
  // El DNS del hotspot de Windows (ICS) suele fallar: se fuerza uno público
  WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(),
              IPAddress(8, 8, 8, 8), IPAddress(1, 1, 1, 1));
  return true;
}

// Registra al nodo como peer para poder enviarle comandos.
// channel = 0 -> usa el canal actual (el del router); ifidx = interfaz STA.
bool registrarPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt = false;
  esp_err_t e = esp_now_add_peer(&peer);
  if (e != ESP_OK) {
    Serial.printf("ERROR: esp_now_add_peer(%s) -> %d\n", macStr(mac).c_str(), (int)e);
    return false;
  }
  return true;
}

// Publica {"entregado":true|false[,"error":"..."]} en nodos/<n>/led/ack
void publicarAck(int n, bool ok, const char* error = nullptr) {
  JsonDocument doc;
  doc["entregado"] = ok;
  if (error) doc["error"] = error;
  char buffer[96];
  size_t len = serializeJson(doc, buffer);
  String topic = TOPIC_NODOS + String(n) + "/led/ack";
  mqtt.publish(topic.c_str(), (const uint8_t*)buffer, len, false);
  Serial.printf("PUB %s %s\n", topic.c_str(), buffer);
}

// Publica la telemetría de un nodo como JSON, con el conteo de tramas perdidas
void publicarTelemetria(int n, const Mensaje& m) {
  // Copias locales: los campos de una estructura packed no se pasan por referencia
  uint32_t seq = m.seq;
  float    t   = m.temperatura;
  float    h   = m.humedad;
  uint16_t luz = m.luz;

  JsonDocument doc;
  doc["grupo"] = n;
  doc["seq"]   = seq;
  if (isnan(t)) doc["temperatura"] = nullptr;
  else          doc["temperatura"] = serialized(String(t, 1));   // 1 decimal
  if (isnan(h)) doc["humedad"] = nullptr;
  else          doc["humedad"] = serialized(String(h, 1));
  doc["luz"]      = luz;
  doc["perdidos"] = perdidos[n];

  char buffer[160];
  size_t len = serializeJson(doc, buffer);
  String topic = TOPIC_NODOS + String(n) + "/telemetria";
  bool ok = mqtt.connected() &&
            mqtt.publish(topic.c_str(), (const uint8_t*)buffer, len, false);
  Serial.printf("PUB %s %s %s\n", topic.c_str(), buffer, ok ? "" : "(FALLO)");
}

// Contexto de loop(): procesa lo que dejó el callback de recepción
void procesarRecepciones() {
  Recepcion r;
  while (xQueueReceive(colaRx, &r, 0) == pdTRUE) {
    int n = r.m.grupo;
    if (n < 1 || n > MAX_GRUPOS) continue;

    // Aprender la MAC del nodo y registrarla como peer (primera vez o si cambia)
    if (!nodoActivo[n] || memcmp(macNodo[n], r.mac, 6) != 0) {
      if (nodoActivo[n] && esp_now_is_peer_exist(macNodo[n])) esp_now_del_peer(macNodo[n]);
      memcpy(macNodo[n], r.mac, 6);
      registrarPeer(r.mac);
      Serial.printf("Nodo %d registrado: %s\n", n, macStr(r.mac).c_str());
    }

    // Tramas perdidas por huecos en seq. Si seq retrocede, el nodo se reinició:
    // no se cuenta como pérdida.
    uint32_t seq = r.m.seq;
    if (nodoActivo[n] && seq > ultimoSeq[n] + 1) {
      perdidos[n] += seq - ultimoSeq[n] - 1;
    }
    nodoActivo[n] = true;
    ultimoSeq[n]  = seq;

    publicarTelemetria(n, r.m);
  }
}

// Publica en led/ack el resultado de cada envío ESP-NOW
void procesarAcks() {
  ResultadoEnvio e;
  while (xQueueReceive(colaAck, &e, 0) == pdTRUE) {
    for (int n = 1; n <= MAX_GRUPOS; n++) {
      if (nodoActivo[n] && memcmp(macNodo[n], e.mac, 6) == 0) {
        publicarAck(n, e.ok);
        break;
      }
    }
  }
}

// Callback MQTT: .../gateway/nodos/<n>/led/control con {"r":255,"g":0,"b":128}
// Se ejecuta dentro de mqtt.loop(), es decir, en el contexto de loop().
// OJO: PubSubClient reutiliza el mismo buffer para recibir y para publicar,
// por eso primero se copia el topic y se lee el JSON, y solo después se publica.
void alRecibirMensaje(char* topic, byte* payload, unsigned int longitud) {
  Serial.printf("SUB %s (%u bytes)\n", topic, longitud);

  // 1. Extraer <n> del topic: TOPIC_NODOS + <n> + "/led/control"
  String t(topic);
  const String sufijo = "/led/control";
  if (!t.startsWith(TOPIC_NODOS) || !t.endsWith(sufijo)) return;
  if (t.length() <= TOPIC_NODOS.length() + sufijo.length()) return;
  String idStr = t.substring(TOPIC_NODOS.length(), t.length() - sufijo.length());
  for (unsigned int i = 0; i < idStr.length(); i++) {
    if (!isDigit(idStr[i])) return;           // solo dígitos
  }
  int n = idStr.toInt();
  if (n < 1 || n > MAX_GRUPOS) {              // validar 1..20
    Serial.printf("  Nodo fuera de rango: %s\n", idStr.c_str());
    return;
  }

  // 2. Validar el JSON (el broker público es abierto: no se confía en nadie)
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, longitud);
  if (error) {
    Serial.printf("  JSON invalido: %s\n", error.c_str());
    publicarAck(n, false, "JSON invalido");
    return;
  }
  if (!doc["r"].is<int>() || !doc["g"].is<int>() || !doc["b"].is<int>()) {
    Serial.println("  Faltan los campos r, g, b");
    publicarAck(n, false, "faltan r,g,b");
    return;
  }
  ComandoLed cmd;
  cmd.r = constrain(doc["r"].as<int>(), 0, 255);
  cmd.g = constrain(doc["g"].as<int>(), 0, 255);
  cmd.b = constrain(doc["b"].as<int>(), 0, 255);

  // 3. El nodo debe estar activo (MAC aprendida de sus tramas)
  if (!nodoActivo[n]) {
    Serial.printf("  Nodo %d desconocido (aun no ha enviado tramas)\n", n);
    publicarAck(n, false, "nodo desconocido");
    return;
  }

  // 4. Enviar por ESP-NOW; el resultado llega al callback onEnviado()
  esp_err_t e = esp_now_send(macNodo[n], (uint8_t*)&cmd, sizeof(cmd));
  Serial.printf("  ESP-NOW -> nodo %d (%s) R:%d G:%d B:%d: %s\n", n,
                macStr(macNodo[n]).c_str(), cmd.r, cmd.g, cmd.b,
                e == ESP_OK ? "enviado" : "FALLO");
  if (e != ESP_OK) publicarAck(n, false, "esp_now_send");
}

// Reconexión no bloqueante: un intento cada PERIODO_REINTENTO ms
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
  // LWT del Adicional 1: "offline" retenido (QoS 1) si el gateway se cae
  bool ok = mqtt.connect(clientId.c_str(), nullptr, nullptr,
                         TOPIC_ESTADO.c_str(), 1, true, "offline", true);
  if (ok) {
    Serial.println("conectado");
    mqtt.publish(TOPIC_ESTADO.c_str(), "online", true);                   // retenido
    mqtt.subscribe((TOPIC_NODOS + "+/led/control").c_str(), 1);           // QoS 1
  } else {
    // -2: sin conexión TCP, 2: Client ID rechazado, 4/5: credenciales
    Serial.printf("fallo (estado %d)\n", mqtt.state());
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);   // tiempo para abrir el Monitor Serie
  Serial.println("=== Adicional 2: Gateway ESP-NOW <-> MQTT ===");

  colaRx  = xQueueCreate(10, sizeof(Recepcion));
  colaAck = xQueueCreate(10, sizeof(ResultadoEnvio));

  WiFi.mode(WIFI_STA);
  uint8_t m[6];
  WiFi.macAddress(m);
  Serial.printf("MAC gateway: %s\n", WiFi.macAddress().c_str());
  Serial.printf("Para el nodo: {0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X}\n",
                m[0], m[1], m[2], m[3], m[4], m[5]);

  if (!conectarWiFi()) {
    Serial.println("Sin red por ahora: loop() seguira reintentando");
  }
  // Sin ahorro de energía: con modem sleep el gateway pierde tramas ESP-NOW
  WiFi.setSleep(false);

  Serial.println("---- Configurar en el nodo ----");
  Serial.printf("MAC gateway: %s\n", WiFi.macAddress().c_str());
  Serial.printf("CANAL_WIFI : %d\n", WiFi.channel());
  Serial.println("-------------------------------");

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: no se pudo inicializar ESP-NOW");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onRecibido);
  esp_now_register_send_cb(onEnviado);

  // Client ID único por placa
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  clientId = String("esp32-gw-") + GRUPO + "-" + mac.substring(6);

  mqtt.setServer(MQTT_BROKER, MQTT_PUERTO);
  mqtt.setCallback(alRecibirMensaje);
  mqtt.setKeepAlive(KEEPALIVE_S);
  mqtt.setBufferSize(512);
  Serial.printf("Topic base: %s\n", TOPIC_BASE.c_str());
}

void loop() {
  mantenerConexion();
  mqtt.loop();            // procesa mensajes entrantes y mantiene el keep-alive
  procesarRecepciones();  // subida: ESP-NOW -> MQTT
  procesarAcks();         // resultado de los envíos -> led/ack

  // Se repite para verlo aunque el Monitor Serie se abra tarde
  if (millis() - ultimoEstado >= PERIODO_ESTADO) {
    ultimoEstado = millis();
    int activos = 0;
    for (int i = 1; i <= MAX_GRUPOS; i++) if (nodoActivo[i]) activos++;
    Serial.printf("[gateway] MAC=%s CANAL_WIFI=%d MQTT=%s nodos=%d\n",
                  WiFi.macAddress().c_str(), WiFi.channel(),
                  mqtt.connected() ? "ok" : "caido", activos);
  }
}
