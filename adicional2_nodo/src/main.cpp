// ============================================================
// Ejercicio Adicional 2: nodo ESP-NOW con medición de tasa de
// entrega y latencia (TX -> ACK de capa MAC)
// Práctica Capa de Red -- IoT USC
// ============================================================
// Uso: poner el nodo a la distancia a medir, abrir el Monitor Serie
// (115200). El nodo envía 50 tramas, imprime el resumen (tasa de
// entrega y latencia media) y espera a que escribas cualquier
// caracter en el Monitor Serie para repetir a la siguiente distancia.
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <DHT.h>

// ---- Configuración ----
#define NUMERO_GRUPO  8
// Receptor = el OTRO ESP32 con el gateway (adicional1_gateway). Al arrancar,
// el gateway imprime "MAC gateway" y "CANAL_WIFI": copiar esos valores aquí.
// Ej.: MAC 24:6F:28:AB:12:F0 -> {0x24, 0x6F, 0x28, 0xAB, 0x12, 0xF0}
#define CANAL_WIFI    1                                   // canal del router (el gateway lo imprime)
uint8_t macMaestro[6] = {0xC8, 0x2E, 0x18, 0x5F, 0x5D, 0x08};  // MAC del gateway C8:2E:18:5F:5D:08

#define N_ENVIOS      50     // envíos por distancia
#define INTERVALO_MS  500    // pausa entre envíos

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

typedef struct __attribute__((packed)) {
  uint8_t  grupo;
  uint32_t seq;
  float    temperatura;
  float    humedad;
  uint16_t luz;
} Mensaje;

DHT dht(PIN_DHT, DHT_TYPE);
Mensaje msg;

// Variables compartidas con el callback (tarea WiFi)
volatile uint32_t tEnvio = 0;          // micros() justo antes de enviar
volatile uint32_t enviosOk = 0;
volatile uint32_t enviosFallidos = 0;
volatile uint64_t sumaLatencia = 0;    // solo envíos con ACK (us)
volatile uint32_t latUltima = 0;
volatile bool     callbackListo = false;

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(CH_R, r);
  ledcWrite(CH_G, g);
  ledcWrite(CH_B, b);
}

// Callback de envío: mide el tiempo hasta el ACK. Sin Serial aquí.
void onEnviado(const uint8_t* mac, esp_now_send_status_t estado) {
  uint32_t lat = micros() - tEnvio;
  if (estado == ESP_NOW_SEND_SUCCESS) {
    enviosOk++;
    sumaLatencia += lat;
    latUltima = lat;
    setColor(0, 255, 0);
  } else {
    enviosFallidos++;
    latUltima = 0;
    setColor(255, 0, 0);
  }
  callbackListo = true;
}

void esperarEnter() {
  while (Serial.available()) Serial.read();
  Serial.println("\nCambie la distancia y escriba cualquier caracter para repetir...");
  while (!Serial.available()) delay(50);
  while (Serial.available()) Serial.read();
}

void resumen() {
  uint32_t total = enviosOk + enviosFallidos;
  float tasa = total ? 100.0f * enviosOk / total : 0;
  float latMedia = enviosOk ? (float)sumaLatencia / enviosOk : 0;
  Serial.println("\n======== RESUMEN ========");
  Serial.printf("Envios totales : %lu\n", (unsigned long)total);
  Serial.printf("Entregados (ok): %lu\n", (unsigned long)enviosOk);
  Serial.printf("Fallidos       : %lu\n", (unsigned long)enviosFallidos);
  Serial.printf("Tasa de entrega: %.1f %%\n", tasa);
  Serial.printf("Latencia media : %.0f us (%.2f ms) [TX->ACK MAC, solo ok]\n",
                latMedia, latMedia / 1000.0f);
  Serial.println("=========================");
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== Adicional 2: ESP-NOW tasa de entrega y latencia ===");

  ledcSetup(CH_R, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_G, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_B, PWM_FREQ, PWM_BITS);
  ledcAttachPin(PIN_LED_R, CH_R);
  ledcAttachPin(PIN_LED_G, CH_G);
  ledcAttachPin(PIN_LED_B, CH_B);
  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(CANAL_WIFI, WIFI_SECOND_CHAN_NONE);
  Serial.printf("MAC de este nodo: %s  Canal: %d\n", WiFi.macAddress().c_str(), CANAL_WIFI);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: no se pudo inicializar ESP-NOW");
    setColor(255, 0, 0);
    while (true) delay(1000);
  }
  esp_now_register_send_cb(onEnviado);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, macMaestro, 6);
  peer.channel = CANAL_WIFI;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("ERROR: no se pudo registrar el peer maestro");
    while (true) delay(1000);
  }

  msg.grupo = NUMERO_GRUPO;
  msg.seq = 0;
  esperarEnter();
}

void loop() {
  // Reiniciar contadores para esta distancia
  enviosOk = 0; enviosFallidos = 0; sumaLatencia = 0;
  Serial.printf("\nIniciando %d envios...\n", N_ENVIOS);

  for (int i = 1; i <= N_ENVIOS; i++) {
    msg.seq++;
    msg.temperatura = dht.readTemperature();
    msg.humedad     = dht.readHumidity();
    msg.luz         = map(analogRead(PIN_LDR), 0, 4095, 0, 100);

    callbackListo = false;
    tEnvio = micros();
    esp_err_t r = esp_now_send(macMaestro, (uint8_t*)&msg, sizeof(msg));
    if (r != ESP_OK) {
      Serial.printf("%2d: esp_now_send error: %s\n", i, esp_err_to_name(r));
      enviosFallidos++;
    } else {
      uint32_t t0 = millis();
      while (!callbackListo && millis() - t0 < 200) delay(1);   // espera el callback
      if (!callbackListo) {
        Serial.printf("%2d: sin callback\n", i);
        enviosFallidos++;
      } else if (latUltima > 0) {
        Serial.printf("%2d: ENTREGADO  lat=%lu us\n", i, (unsigned long)latUltima);
      } else {
        Serial.printf("%2d: FALLO\n", i);
      }
    }
    delay(INTERVALO_MS);
  }

  resumen();
  esperarEnter();
}