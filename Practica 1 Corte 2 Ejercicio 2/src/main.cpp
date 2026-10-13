// ============================================================
// Ejercicio 2: Servidor BLE GATT + nRF Connect
// Práctica Capa de Red -- IoT USC
// ============================================================

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <DHT.h>

// ---- Identificación del grupo (modificar) ----
#define NOMBRE_BLE  "ESP32-G08"

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
#define PIN_DHT   33
#define DHT_TYPE  DHT22


// ---- UUIDs ----
// Servicio y características estándar del Bluetooth SIG (16 bits)
#define UUID_SERV_AMBIENTAL  BLEUUID((uint16_t)0x181A)  // Environmental Sensing
#define UUID_CHAR_TEMP       BLEUUID((uint16_t)0x2A6E)  // Temperature (sint16, 0.01 C)
#define UUID_CHAR_HUM        BLEUUID((uint16_t)0x2A6F)  // Humidity (uint16, 0.01 %)
// Servicio propio (128 bits) para controlar el LED RGB
#define UUID_SERV_LED  "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define UUID_CHAR_LED  "6e400002-b5a3-f393-e0a9-e50e24dcca9e"

DHT dht(PIN_DHT, DHT_TYPE);
BLEServer*         servidor = nullptr;
BLECharacteristic* charTemp = nullptr;
BLECharacteristic* charHum  = nullptr;
bool clienteConectado = false;

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(CH_R, r);
  ledcWrite(CH_G, g);
  ledcWrite(CH_B, b);
}

// ---- Callbacks GAP: conexión y desconexión del cliente ----
class ServidorCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    clienteConectado = true;
    Serial.println(">> Cliente BLE conectado");
  }
  void onDisconnect(BLEServer* s) override {
    clienteConectado = false;
    Serial.println(">> Cliente desconectado. Reiniciando advertising...");
    BLEDevice::startAdvertising();  // volver a ser visible
  }
};

// ---- Callback GATT: el cliente escribe en la característica LED ----
// Formato esperado: 3 bytes [R, G, B], p. ej. FF0000 = rojo
class LedCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    std::string v = c->getValue();
    if (v.length() != 3) {
      Serial.printf("Escritura LED ignorada: %d bytes (se esperan 3)\n", (int)v.length());
      return;
    }
    uint8_t r = (v[0]-0x30)*25, g = (v[1]-0x30)*25, b = (v[2]-0x30)*25;
    setColor(r, g, b);
    Serial.printf("Escritura LED -> R:%d G:%d B:%d\n", r, g, b);
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("=== Ejercicio 2: BLE GATT ===");

  ledcSetup(CH_R, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_G, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_B, PWM_FREQ, PWM_BITS);
  ledcAttachPin(PIN_LED_R, CH_R);
  ledcAttachPin(PIN_LED_G, CH_G);
  ledcAttachPin(PIN_LED_B, CH_B);
  dht.begin();

  // 1. Inicializar la pila BLE con el nombre del dispositivo (GAP)
  BLEDevice::init(NOMBRE_BLE);
  servidor = BLEDevice::createServer();
  servidor->setCallbacks(new ServidorCallbacks());

  // 2. Servicio Environmental Sensing con dos características Read + Notify
  BLEService* servAmb = servidor->createService(UUID_SERV_AMBIENTAL);
  charTemp = servAmb->createCharacteristic(UUID_CHAR_TEMP,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  charTemp->addDescriptor(new BLE2902());  // CCCD: permite activar Notify
  charHum = servAmb->createCharacteristic(UUID_CHAR_HUM,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  charHum->addDescriptor(new BLE2902());
  servAmb->start();

  // 3. Servicio propio con una característica Write para el LED
  BLEService* servLed = servidor->createService(UUID_SERV_LED);
  BLECharacteristic* charLed = servLed->createCharacteristic(UUID_CHAR_LED,
      BLECharacteristic::PROPERTY_WRITE);
  charLed->setCallbacks(new LedCallbacks());
  servLed->start();

  // 4. Advertising: anunciar el servicio ambiental
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(UUID_SERV_AMBIENTAL);
  adv->setScanResponse(true);
  adv->setMinInterval(160);  // 160 x 0.625 ms = 100 ms
  adv->setMaxInterval(320);  // 320 x 0.625 ms = 200 ms
  BLEDevice::startAdvertising();

  Serial.printf("Advertising como \"%s\"\n", NOMBRE_BLE);
  Serial.printf("MAC BLE: %s\n", BLEDevice::getAddress().toString().c_str());
}

void loop() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    Serial.println("DHT22: error de lectura (NaN)");
  } else {
    // Codificación del Bluetooth SIG: entero en centésimas, little-endian
    int16_t  tBle = (int16_t)(t * 100);
    uint16_t hBle = (uint16_t)(h * 100);
    charTemp->setValue((uint8_t*)&tBle, 2);
    charHum->setValue((uint8_t*)&hBle, 2);

    if (clienteConectado) {
      charTemp->notify();   // solo llega si el cliente activó Notify
      charHum->notify();
    }
    Serial.printf("T=%.2f C (0x%04X)  H=%.2f %% (0x%04X)  conectado=%s\n",
                  t, (uint16_t)tBle, h, hBle, clienteConectado ? "si" : "no");
  }
  delay(2000);
}
