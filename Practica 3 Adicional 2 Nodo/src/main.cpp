// ============================================================
// Ejercicio Adicional 2: ESP-NOW -- NODO sensor + control remoto del LED (grupo A)
// Basado en el Ejercicio 3 de la Práctica Capa de Red -- IoT USC
// ============================================================
// Cambios respecto al Ejercicio 3:
//   1. Estructura ComandoLed (3 bytes) y callback onRecibido() que solo acepta
//      tramas de 3 bytes provenientes de la MAC del gateway.
//   2. onEnviado() ya no cambia el LED: el resultado del envío se muestra solo
//      en el Monitor Serie para no pisar el color recibido.
// La estructura Mensaje NO cambia.
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <DHT.h>

// ---- Configuración (modificar con los datos que imprime el gateway) ----
#define NUMERO_GRUPO  8
#define CANAL_WIFI    1     // = CANAL_WIFI que imprime el gateway (canal del router)
// MAC STA del gateway (la que imprime WiFi.macAddress() en el gateway)
uint8_t macMaestro[6] = {0xC8, 0x2E, 0x18, 0x5F, 0x5D, 0x08};
//C8:2E:18:5F:5D:08
// Pines LED RGB y canales LEDC
#define PIN_LED_R  26
#define PIN_LED_G  27
#define PIN_LED_B  14
#define CH_R       0
#define CH_G       1
#define CH_B       2
#define PWM_FREQ   5000
#define PWM_BITS   8

#define PIN_LDR   34
#define PIN_DHT   33
#define DHT_TYPE  DHT22

// Estructura del mensaje: debe ser IDÉNTICA en el nodo y en el maestro
// (packed evita bytes de relleno; tamaño total = 15 bytes <= 250)
typedef struct __attribute__((packed)) {
  uint8_t  grupo;
  uint32_t seq;          // número de secuencia para detectar pérdidas
  float    temperatura;
  float    humedad;
  uint16_t luz;          // porcentaje 0-100
} Mensaje;

// Comando de color que envía el gateway (3 bytes)
typedef struct __attribute__((packed)) { uint8_t r, g, b; } ComandoLed;

DHT dht(PIN_DHT, DHT_TYPE);
Mensaje msg;
uint32_t enviosOk = 0, enviosFallidos = 0;

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(CH_R, r);
  ledcWrite(CH_G, g);
  ledcWrite(CH_B, b);
}

// Callback de envío: informa si el maestro confirmó la trama (ACK de capa MAC).
// Solo escribe en el Monitor Serie: el LED lo controla el dashboard.
void onEnviado(const uint8_t* mac, esp_now_send_status_t estado) {
  if (estado == ESP_NOW_SEND_SUCCESS) {
    enviosOk++;
  } else {
    enviosFallidos++;
  }
  Serial.printf("  -> %s  (ok=%lu, fallos=%lu)\n",
                estado == ESP_NOW_SEND_SUCCESS ? "ENTREGADO" : "FALLO",
                enviosOk, enviosFallidos);
}

// Callback de recepción: aplica el color que manda el gateway.
// Solo acepta tramas de exactamente 3 bytes enviadas desde la MAC del gateway.
void onRecibido(const uint8_t* mac, const uint8_t* datos, int len) {
  if (len != (int)sizeof(ComandoLed)) return;        // tamaño incorrecto
  if (memcmp(mac, macMaestro, 6) != 0) return;       // no viene del gateway
  ComandoLed cmd;
  memcpy(&cmd, datos, sizeof(cmd));
  setColor(cmd.r, cmd.g, cmd.b);
  Serial.printf("  <- COMANDO LED  R:%u G:%u B:%u\n", cmd.r, cmd.g, cmd.b);
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== Adicional 2: ESP-NOW nodo (envia y recibe) ===");

  ledcSetup(CH_R, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_G, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_B, PWM_FREQ, PWM_BITS);
  ledcAttachPin(PIN_LED_R, CH_R);
  ledcAttachPin(PIN_LED_G, CH_G);
  ledcAttachPin(PIN_LED_B, CH_B);
  dht.begin();

  // 1. ESP-NOW usa la radio WiFi en modo estación, sin conectarse a un AP
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(CANAL_WIFI, WIFI_SECOND_CHAN_NONE);
  Serial.printf("MAC de este nodo: %s\n", WiFi.macAddress().c_str());
  Serial.printf("Canal: %d\n", CANAL_WIFI);

  // 2. Inicializar ESP-NOW y registrar los callbacks de envío y recepción
  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: no se pudo inicializar ESP-NOW");
    setColor(255, 0, 0);
    while (true) delay(1000);
  }
  esp_now_register_send_cb(onEnviado);
  esp_now_register_recv_cb(onRecibido);

  // 3. Registrar al maestro (gateway) como peer (unicast)
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, macMaestro, 6);
  peer.channel = CANAL_WIFI;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("ERROR: no se pudo registrar el peer maestro");
    while (true) delay(1000);
  }
  Serial.printf("Maestro registrado: %02X:%02X:%02X:%02X:%02X:%02X\n",
                macMaestro[0], macMaestro[1], macMaestro[2],
                macMaestro[3], macMaestro[4], macMaestro[5]);

  msg.grupo = NUMERO_GRUPO;
  msg.seq = 0;
}

void loop() {
  // Leer sensores y llenar el mensaje
  msg.seq++;
  msg.temperatura = dht.readTemperature();
  msg.humedad     = dht.readHumidity();
  msg.luz         = map(analogRead(PIN_LDR), 0, 4095, 0, 100);

  Serial.printf("Enviando seq=%lu  T=%.1f  H=%.1f  Luz=%u%%\n",
                msg.seq, msg.temperatura, msg.humedad, msg.luz);

  esp_err_t r = esp_now_send(macMaestro, (uint8_t*)&msg, sizeof(msg));
  if (r != ESP_OK) {
    Serial.printf("  esp_now_send error: %s\n", esp_err_to_name(r));
  }

  delay(2000);
}
