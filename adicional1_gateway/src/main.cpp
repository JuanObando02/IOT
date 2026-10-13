// ============================================================
// Ejercicio Adicional 1: Gateway ESP-NOW -> WiFi (HTTP)
// Práctica Capa de Red -- IoT USC
// ============================================================
// Recibe tramas ESP-NOW de los nodos y las publica en:
//   /     tabla HTML (se recarga cada 5 s)
//   /api  arreglo JSON con la última lectura de cada grupo
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>

// ---- Credenciales de la red (modificar). Solo 2.4 GHz, WPA2-Personal ----
const char* WIFI_SSID = "USC-LIBRE-1";
const char* WIFI_PASS = "juan12345";


#define MAX_GRUPOS 20

// Misma estructura que en el nodo y en el maestro
typedef struct __attribute__((packed)) {
  uint8_t  grupo;
  uint32_t seq;
  float    temperatura;
  float    humedad;
  uint16_t luz;
} Mensaje;

typedef struct {
  bool          activo;
  uint8_t       mac[6];
  Mensaje       ultimo;
  uint32_t      recibidos;
  uint32_t      perdidos;
  unsigned long tUltimo;
} EstadoGrupo;

EstadoGrupo grupos[MAX_GRUPOS + 1];   // índice = número de grupo
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
WebServer server(80);

// Callback de recepción (tarea WiFi): sin Serial, solo guarda el dato
void onRecibido(const uint8_t* mac, const uint8_t* datos, int len) {
  if (len != sizeof(Mensaje)) return;
  Mensaje m;
  memcpy(&m, datos, sizeof(m));
  if (m.grupo == 0 || m.grupo > MAX_GRUPOS) return;

  portENTER_CRITICAL(&mux);
  EstadoGrupo& g = grupos[m.grupo];
  // Si seq retrocede, el nodo se reinició: no se cuenta como pérdida
  if (g.activo && m.seq > g.ultimo.seq + 1) {
    g.perdidos += m.seq - g.ultimo.seq - 1;
  }
  g.activo = true;
  memcpy(g.mac, mac, 6);
  g.ultimo = m;
  g.recibidos++;
  g.tUltimo = millis();
  portEXIT_CRITICAL(&mux);
}

// Copia consistente del estado de un grupo
EstadoGrupo copiarGrupo(int i) {
  portENTER_CRITICAL(&mux);
  EstadoGrupo g = grupos[i];
  portEXIT_CRITICAL(&mux);
  return g;
}

// Número con 1 decimal, o "null" (JSON) / "--" (HTML) si es NaN
String num(float v, bool json) {
  if (isnan(v)) return json ? "null" : "--";
  return String(v, 1);
}

String macStr(const uint8_t* m) {
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(b);
}

void handleRaiz() {
  String h = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  h += "<meta name='viewport' content='width=device-width'>";
  h += "<meta http-equiv='refresh' content='5'>";
  h += "<title>Gateway ESP-NOW</title>";
  h += "<style>body{font-family:sans-serif}table{border-collapse:collapse}";
  h += "td,th{border:1px solid #888;padding:4px 8px;text-align:right}</style></head><body>";
  h += "<h2>Gateway ESP-NOW &rarr; WiFi</h2><table>";
  h += "<tr><th>Grupo</th><th>MAC</th><th>Seq</th><th>T [&deg;C]</th><th>H [%]</th>";
  h += "<th>Luz [%]</th><th>Rx</th><th>Perd</th><th>Hace [s]</th></tr>";
  int activos = 0;
  for (int i = 1; i <= MAX_GRUPOS; i++) {
    EstadoGrupo g = copiarGrupo(i);
    if (!g.activo) continue;
    activos++;
    h += "<tr><td>" + String(i) + "</td><td>" + macStr(g.mac) + "</td>";
    h += "<td>" + String(g.ultimo.seq) + "</td>";
    h += "<td>" + num(g.ultimo.temperatura, false) + "</td>";
    h += "<td>" + num(g.ultimo.humedad, false) + "</td>";
    h += "<td>" + String(g.ultimo.luz) + "</td>";
    h += "<td>" + String(g.recibidos) + "</td>";
    h += "<td>" + String(g.perdidos) + "</td>";
    h += "<td>" + String((millis() - g.tUltimo) / 1000) + "</td></tr>";
  }
  h += "</table>";
  if (activos == 0) h += "<p>Esperando datos de los nodos...</p>";
  h += "<p><a href='/api'>/api (JSON)</a></p></body></html>";
  server.send(200, "text/html", h);
}

void handleApi() {
  String json = "[";
  bool primero = true;
  for (int i = 1; i <= MAX_GRUPOS; i++) {
    EstadoGrupo g = copiarGrupo(i);
    if (!g.activo) continue;
    if (!primero) json += ",";
    primero = false;
    json += "{\"grupo\":" + String(i);
    json += ",\"mac\":\"" + macStr(g.mac) + "\"";
    json += ",\"temperatura\":" + num(g.ultimo.temperatura, true);
    json += ",\"humedad\":" + num(g.ultimo.humedad, true);
    json += ",\"luz\":" + String(g.ultimo.luz);
    json += ",\"seq\":" + String(g.ultimo.seq);
    json += ",\"recibidos\":" + String(g.recibidos);
    json += ",\"perdidos\":" + String(g.perdidos);
    json += ",\"hace_s\":" + String((millis() - g.tUltimo) / 1000);
    json += "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleNoEncontrado() {
  server.send(404, "text/plain", "Recurso no encontrado");
}

void setup() {
  Serial.begin(115200);
  delay(3000);   // tiempo para abrir el Monitor Serie antes de imprimir
  Serial.println("=== Adicional 1: Gateway ESP-NOW -> WiFi ===");

  // WIFI_STA basta: ESP-NOW recibe por la interfaz STA
  WiFi.mode(WIFI_STA);
  delay(500);

  // MAC al inicio (antes de conectar), por si luego falla la conexion
  uint8_t m[6];
  WiFi.macAddress(m);
  Serial.printf("MAC gateway: %s\n", WiFi.macAddress().c_str());
  Serial.printf("Para el nodo: {0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X}\n",
                m[0], m[1], m[2], m[3], m[4], m[5]);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Conectando a \"%s\"", WIFI_SSID);
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - inicio > 20000) {
      Serial.println("\nERROR: sin conexion, reiniciando...");
      delay(1000);
      ESP.restart();
    }
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nConectado!");

  // Sin ahorro de energia: con modem sleep el gateway pierde tramas ESP-NOW
  WiFi.setSleep(false);

  // Datos que debe usar el nodo del grupo A
  Serial.println("---- Configurar en el nodo ----");
  Serial.printf("IP gateway : %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("MAC gateway: %s\n", WiFi.macAddress().c_str());
  Serial.printf("CANAL_WIFI : %d\n", WiFi.channel());
  Serial.println("-------------------------------");

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: no se pudo inicializar ESP-NOW");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onRecibido);

  server.on("/", handleRaiz);
  server.on("/api", handleApi);
  server.onNotFound(handleNoEncontrado);
  server.begin();
  Serial.printf("Servidor listo: http://%s/\n", WiFi.localIP().toString().c_str());
}

void loop() {
  server.handleClient();

  static unsigned long ultimoChequeo = 0;
  if (millis() - ultimoChequeo >= 5000) {
    ultimoChequeo = millis();
    // Se repite cada 5 s para verlo aunque el Monitor Serie se abra tarde
    Serial.printf("[gateway] MAC=%s  IP=%s  CANAL_WIFI=%d\n",
                  WiFi.macAddress().c_str(), WiFi.localIP().toString().c_str(),
                  WiFi.channel());
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi desconectado, reintentando...");
      WiFi.reconnect();
    }
  }
}