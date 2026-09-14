// ============================================================
// Ejercicio 1: Control LED RGB con PWM (módulo LEDC)
// Práctica Capa de Dispositivos -- IoT USC
// ============================================================

#include <Arduino.h>

// Pines del LED RGB (cátodo común)
#define PIN_LED_R  25
#define PIN_LED_G  26
#define PIN_LED_B  27

// Canales LEDC (el ESP32 tiene 16 canales disponibles: 0-15)
#define CH_R  0
#define CH_G  1
#define CH_B  2

// Configuración PWM
#define PWM_FREQ  5000   // Frecuencia: 5 kHz
#define PWM_BITS  8      // Resolución: 8 bits (0-255)

// Establece el color RGB del LED mediante PWM
void setColor(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(CH_R, r);
  ledcWrite(CH_G, g);
  ledcWrite(CH_B, b);
  Serial.printf("\nColor -> R:%3d  G:%3d  B:%3d\n", r, g, b);
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== Ejercicio 1: LED RGB con PWM ===");

  // Configurar los tres canales LEDC
  ledcSetup(CH_R, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_G, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_B, PWM_FREQ, PWM_BITS);

  // Asignar cada canal a su pin GPIO
  ledcAttachPin(PIN_LED_R, CH_R);
  ledcAttachPin(PIN_LED_G, CH_G);
  ledcAttachPin(PIN_LED_B, CH_B);

  Serial.println("PWM configurado: 5 kHz, 8 bits, 3 canales");
}

void loop() {
  // --- Secuencia de colores básicos ---
  Serial.println("-- Secuencia de colores --");
  setColor(255,   0,   0); delay(800);  // Rojo
  setColor(  0, 255,   0); delay(800);  // Verde
  setColor(  0,   0, 255); delay(800);  // Azul
  setColor(255, 255, 255); delay(800);  // Blanco
  setColor(255, 255,   0); delay(800);  // Amarillo
  setColor(  0, 255, 255); delay(800);  // Cian
  setColor(255,   0, 255); delay(800);  // Magenta
  setColor(  0,   0,   0); delay(800);  // Apagado

  // --- Efecto fade en canal rojo: 0 -> 255 -> 0 ---
  Serial.println("-- Efecto fade rojo --");
  for (int v = 0; v <= 255; v += 5) {
    setColor(v, 0, 0);
    delay(20);
  }
  for (int v = 255; v >= 0; v -= 5) {
    setColor(v, 0, 0);
    delay(20);
  }
  delay(500);
}