// ============================================================
// Ejercicio Adicional 1: Sistema de Semaforo Ambiental
// Practica Capa de Dispositivos -- IoT USC
//
// Conexiones: las mismas de "Practica 1 Ejercicio 3"
//   LED RGB (anodo comun via PWM) -> R:26  G:27  B:14
//   LDR (ADC)                     -> 34
//   DHT22 (data)                  -> 33
//   HC-SR04 TRIG                  -> 25
//   HC-SR04 ECHO                  -> 32
// ============================================================

#include <Arduino.h>
#include <DHT.h>

// ================= PINES Y CONFIGURACION ====================

#define PIN_LED_R  26
#define PIN_LED_G  27
#define PIN_LED_B  14
#define CH_R       0
#define CH_G       1
#define CH_B       2
#define PWM_FREQ   5000
#define PWM_BITS   8

#define PIN_LDR    34    // GPIO de entrada analogica exclusiva (ADC)
#define PIN_DHT    33    // DHT22 DATA (one-wire)
#define PIN_TRIG   25    // HC-SR04 trigger (salida)
#define PIN_ECHO   32    // HC-SR04 echo   (entrada)
#define DHT_TYPE   DHT22

DHT dht(PIN_DHT, DHT_TYPE);

// ================= MAQUINA DE ESTADOS ========================
// Prioridad (de mayor a menor): OBJETO_PROXIMO > PELIGRO_TEMP >
// ALERTA_TEMP > LUZ_BAJA > NORMAL
enum Estado { NORMAL, ALERTA_TEMP, PELIGRO_TEMP, LUZ_BAJA, OBJETO_PROXIMO };

Estado evaluarEstado(float temp, int porcLuz, float dist) {
  if (dist > 0.0f && dist < 20.0f) return OBJETO_PROXIMO;
  if (temp > 32.0f)                return PELIGRO_TEMP;
  if (temp > 28.0f)                return ALERTA_TEMP;
  if (porcLuz < 20)                return LUZ_BAJA;
  return NORMAL;
}

const char *nombreEstado(Estado e) {
  switch (e) {
    case NORMAL:         return "NORMAL";
    case ALERTA_TEMP:     return "ALERTA_TEMP";
    case PELIGRO_TEMP:    return "PELIGRO_TEMP";
    case LUZ_BAJA:        return "LUZ_BAJA";
    case OBJETO_PROXIMO:  return "OBJETO_PROXIMO";
  }
  return "?";
}

// ================= FUNCIONES AUXILIARES =====================

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(CH_R, r);
  ledcWrite(CH_G, g);
  ledcWrite(CH_B, b);
}

void aplicarEstado(Estado e) {
  switch (e) {
    case NORMAL:        setColor(  0, 255,   0); break; // verde
    case ALERTA_TEMP:   setColor(255, 165,   0); break; // amarillo
    case PELIGRO_TEMP:  setColor(255,   0,   0); break; // rojo
    case LUZ_BAJA:       setColor(  0,   0, 255); break; // azul
    case OBJETO_PROXIMO:
      // Parpadeo rojo
      setColor(255, 0, 0); delay(150);
      setColor(  0, 0, 0); delay(150);
      break;
  }
}

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

// ================= SETUP =====================

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== IoT USC - Semaforo Ambiental ===");

  ledcSetup(CH_R, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_G, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_B, PWM_FREQ, PWM_BITS);
  ledcAttachPin(PIN_LED_R, CH_R);
  ledcAttachPin(PIN_LED_G, CH_G);
  ledcAttachPin(PIN_LED_B, CH_B);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);

  dht.begin();

  Serial.println("Estabilizando sensores... (2 segundos)");
  delay(2000);
}

// ================= LOOP =====================

void loop() {
  Serial.println("\n------------------------------------------------");

  // --- LDR ---
  int valorLDR = analogRead(PIN_LDR);
  int porcLuz  = constrain(map(valorLDR, 1600, 4095, 0, 100), 0, 100);

  // --- DHT22 (si falla la lectura se conserva la ultima temperatura valida) ---
  static float ultimaTemp = 25.0f;
  float temperatura = dht.readTemperature();
  float humedad     = dht.readHumidity();
  bool  lecturaValida = !isnan(temperatura) && !isnan(humedad);
  if (lecturaValida) {
    ultimaTemp = temperatura;
  } else {
    temperatura = ultimaTemp;
    Serial.println("DHT22:  Error de lectura (NaN), se usa la ultima temperatura valida");
  }

  // --- HC-SR04 ---
  float distancia = medirDistancia();

  // --- Maquina de estados ---
  Estado estado = evaluarEstado(temperatura, porcLuz, distancia);
  aplicarEstado(estado);

  Serial.printf("LDR:     %4d ADC -> Luz: %3d%%\r\n", valorLDR, porcLuz);
  if (lecturaValida) {
    Serial.printf("DHT22:   Temperatura = %.1f C  |  Humedad = %.1f %%\r\n", temperatura, humedad);
  }
  if (distancia > 0) {
    Serial.printf("HC-SR04: Distancia = %.1f cm\r\n", distancia);
  } else {
    Serial.println("HC-SR04: Sin respuesta o fuera de rango");
  }
  Serial.printf("Estado:  %s\r\n", nombreEstado(estado));

  // Espera general de 2 segundos requerida por el DHT22
  delay(2000);
}
