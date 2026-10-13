// ============================================================
// Ejercicio 1: WiFi en modo STA + servidor web
// Práctica Capa de Red -- IoT USC
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>

// ---- Credenciales de la red (modificar) ----
const char* WIFI_SSID = "MANI CON PASAS";
const char* WIFI_PASS = "juan5492814,";

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
WebServer server(80);   // Servidor HTTP en el puerto 80

// Últimas lecturas (se actualizan cada 2 s en loop)
float temperatura = NAN;
float humedad     = NAN;
int   porcLuz     = 0;
uint8_t ledR = 0, ledG = 0, ledB = 0;
unsigned long ultimaLectura = 0;

// Intervalo de impresión del RSSI por el Monitor Serie
#define INTERVALO_RSSI_MS 5000
unsigned long ultimoRssi = 0;

// ---- Funciones auxiliares ----

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  ledR = r; ledG = g; ledB = b;
  ledcWrite(CH_R, r);
  ledcWrite(CH_G, g);
  ledcWrite(CH_B, b);
}

const char* tipoCifrado(wifi_auth_mode_t modo) {
  switch (modo) {
    case WIFI_AUTH_OPEN:            return "Abierta";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Ent";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    default:                        return "Otro";
  }
}

// Escanea las redes WiFi visibles y las imprime en el Monitor Serie
void escanearRedes() {
  Serial.println("Escaneando redes WiFi...");
  int n = WiFi.scanNetworks();
  Serial.printf("%d redes encontradas\n", n);
  Serial.println(" #  SSID                             RSSI  Canal  Cifrado");
  for (int i = 0; i < n; i++) {
    Serial.printf("%2d  %-32s %4d  %5d  %s\n", i + 1,
                  WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i),
                  tipoCifrado(WiFi.encryptionType(i)));
  }
  WiFi.scanDelete();
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
  Serial.printf("  IP:      %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("  Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
  Serial.printf("  MAC:     %s\n", WiFi.macAddress().c_str());
  Serial.printf("  RSSI:    %d dBm\n", WiFi.RSSI());
  Serial.printf("  Canal:   %d\n", WiFi.channel());
  return true;
}

// ---- Manejadores HTTP ----

// GET /  -> página HTML que se recarga cada 5 s
void handleRaiz() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width'>";
  html += "<meta http-equiv='refresh' content='5'>";
  html += "<title>ESP32 IoT</title></head><body style='font-family:sans-serif'>";
  html += "<h2>Nodo ESP32 -- Capa de Red</h2>";
  html += "<p>Temperatura: <b>" + String(temperatura, 1) + " &deg;C</b></p>";
  html += "<p>Humedad: <b>" + String(humedad, 1) + " %</b></p>";
  html += "<p>Luz: <b>" + String(porcLuz) + " %</b></p>";
  html += "<p>RSSI: <b>" + String(WiFi.RSSI()) + " dBm</b></p>";
  html += "<p>LED: <a href='/led?r=255&g=0&b=0'>Rojo</a> | ";
  html += "<a href='/led?r=0&g=255&b=0'>Verde</a> | ";
  html += "<a href='/led?r=0&g=0&b=255'>Azul</a> | ";
  html += "<a href='/led?r=0&g=0&b=0'>Apagar</a></p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// GET /led?r=..&g=..&b=..  -> cambia el color del LED
void handleLed() {
  if (!server.hasArg("r") || !server.hasArg("g") || !server.hasArg("b")) {
    server.send(400, "text/plain", "Faltan parametros r, g, b");
    return;
  }
  setColor(constrain(server.arg("r").toInt(), 0, 255),
           constrain(server.arg("g").toInt(), 0, 255),
           constrain(server.arg("b").toInt(), 0, 255));
  Serial.printf("HTTP /led -> R:%d G:%d B:%d (cliente %s)\n", ledR, ledG, ledB,
                server.client().remoteIP().toString().c_str());
  server.sendHeader("Location", "/");   // redirige a la página principal
  server.send(303);
}

// GET /api  -> datos en formato JSON
void handleApi() {
  String json = "{";
  // Si el DHT22 falla (NaN) se envía null para que el JSON siga siendo válido
  json += "\"temperatura\":" + (isnan(temperatura) ? String("null") : String(temperatura, 1)) + ",";
  json += "\"humedad\":" + (isnan(humedad) ? String("null") : String(humedad, 1)) + ",";
  json += "\"luz\":" + String(porcLuz) + ",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"uptime_ms\":" + String(millis());
  json += "}";
  server.send(200, "application/json", json);
}

void handleNoEncontrado() {
  server.send(404, "text/plain", "Recurso no encontrado");
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== Ejercicio 1: WiFi STA + servidor web ===");

  ledcSetup(CH_R, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_G, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_B, PWM_FREQ, PWM_BITS);
  ledcAttachPin(PIN_LED_R, CH_R);
  ledcAttachPin(PIN_LED_G, CH_G);
  ledcAttachPin(PIN_LED_B, CH_B);
  dht.begin();

  WiFi.mode(WIFI_STA);    // modo estación (cliente de un AP)
  WiFi.disconnect();
  delay(100);

  escanearRedes();
  if (!conectarWiFi()) {
    setColor(255, 0, 0);  // rojo fijo: sin red
    return;
  }

  server.on("/", handleRaiz);
  server.on("/led", handleLed);
  server.on("/api", handleApi);
  server.onNotFound(handleNoEncontrado);
  server.begin();
  Serial.printf("Servidor web listo: http://%s/\n",
                WiFi.localIP().toString().c_str());
}

void loop() {
  server.handleClient();  // atiende peticiones HTTP pendientes

  if (millis() - ultimaLectura >= 2000) {
    ultimaLectura = millis();
    temperatura = dht.readTemperature();
    humedad     = dht.readHumidity();
    porcLuz     = map(analogRead(PIN_LDR), 0, 4095, 0, 100);

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi desconectado, reintentando...");
      WiFi.reconnect();
    }
  }

  // Muestra periódicamente la intensidad de la señal WiFi
  if (millis() - ultimoRssi >= INTERVALO_RSSI_MS) {
    ultimoRssi = millis();
    if (WiFi.status() == WL_CONNECTED) {
      int rssi = WiFi.RSSI();
      // Rangos: >= -50 Excelente | -51..-67 Buena | -68..-80 Regular | < -80 Mala
      const char* calidad = rssi >= -50 ? "Excelente" :
                            rssi >= -67 ? "Buena" :
                            rssi >= -80 ? "Regular" : "Mala";
      Serial.printf("[%lu s] RSSI: %d dBm (%s)\n", millis() / 1000, rssi, calidad);
    } else {
      Serial.println("RSSI: sin conexion WiFi");
    }
  }
}