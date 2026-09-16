// ============================================================
// Ejercicio 2: LDR + DHT22 + HC-SR04 con control de brillo
// Práctica Capa de Dispositivos -- IoT USC
// ============================================================

#include <Arduino.h>
#include <DHT.h>

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
#define PIN_LDR   34    // GPIO de entrada analógica exclusiva (ADC)
#define PIN_DHT   33    // DHT22 DATA (one-wire)
#define PIN_TRIG  25    // HC-SR04 trigger (salida)
#define PIN_ECHO  32    // HC-SR04 echo   (entrada)
#define DHT_TYPE  DHT22

DHT dht(PIN_DHT, DHT_TYPE);

// ---- Funciones auxiliares ----

// Establece color RGB del LED
void setColor(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(CH_R, r);
  ledcWrite(CH_G, g);
  ledcWrite(CH_B, b);
}

// Ajusta el LED a blanco con brillo proporcional al porcentaje (0-100)
void ajustarBrilloLED(int porcentaje) {
  porcentaje = constrain(porcentaje, 0, 100);
  uint8_t val = (uint8_t) map(porcentaje, 0, 100, 0, 255);
  setColor(val, val, val);
}

// Mide distancia con HC-SR04; devuelve cm o -1 si hay timeout
float medirDistancia() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long duracion = pulseIn(PIN_ECHO, HIGH, 30000); // timeout 30 ms
  if (duracion == 0) return -1.0f;
  return (duracion * 0.0343f) / 2.0f;
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== Ejercicio 2: LDR + DHT22 + HC-SR04 ===");

  // Configurar LED PWM
  ledcSetup(CH_R, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_G, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_B, PWM_FREQ, PWM_BITS);
  ledcAttachPin(PIN_LED_R, CH_R);
  ledcAttachPin(PIN_LED_G, CH_G);
  ledcAttachPin(PIN_LED_B, CH_B);

  // Configurar HC-SR04
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);

  // Inicializar DHT22
  dht.begin();
  delay(2000); // Tiempo de estabilización del DHT22

  Serial.println("Sistema listo. Leyendo sensores cada 2 s...");
}

void loop() {
  // 1. Leer LDR y ajustar brillo del LED
  int valorLDR = analogRead(PIN_LDR);           // rango 0-4095
  int porcLuz  = map(valorLDR, 1600, 4095, 0, 100);
  ajustarBrilloLED(porcLuz);

  // 2. Leer DHT22 con validación
  float temperatura = dht.readTemperature();
  float humedad     = dht.readHumidity();
  bool dhtOk = !isnan(temperatura) && !isnan(humedad);

  // 3. Medir distancia ultrasónica
  float distancia = medirDistancia();

  // 4. Mostrar resultados por Monitor Serie
  Serial.println("-------------------------------");
  Serial.printf("LDR:    %4d ADC -> Brillo: %3d%%\r\n", valorLDR, porcLuz);

  if (dhtOk) {
    Serial.printf("DHT22:  Temperatura = %.1f C  |  Humedad = %.1f %%\r\n",
                  temperatura, humedad);
  } else {
    Serial.println("DHT22:  Error de lectura (NaN) -- verificar conexion");
  }

  if (distancia > 0) {
    Serial.printf("HC-SR04: Distancia = %.1f cm\r\n", distancia);
  } else {
    Serial.println("HC-SR04: Sin respuesta (fuera de rango o timeout)");
  }

  delay(2000); // DHT22 requiere mínimo 2 s entre lecturas
}
