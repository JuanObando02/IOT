// ============================================================
// Ejercicio 1: Control LED RGB con PWM (PlatformIO - Core 2.x)
// Práctica Capa de Dispositivos -- IoT USC
// ============================================================

#include <Arduino.h>
#include <DHT.h>

// Pines del LED RGB (cátodo común)
#define PIN_LED_R  25
#define PIN_LED_G  26
#define PIN_LED_B  27

// Pines sensores
#define PIN_LDR   34    // GPIO de entrada analógica exclusiva (ADC)
#define PIN_DHT    4    // DHT22 DATA (one-wire)
#define PIN_TRIG   5    // HC-SR04 trigger (salida)
#define PIN_ECHO  18    // HC-SR04 echo   (entrada)
#define DHT_TYPE  DHT22

// Canales LEDC (obligatorios en PlatformIO / Core 2.x)
#define CH_R  0
#define CH_G  1
#define CH_B  2

// Configuración PWM
#define PWM_FREQ  5000   // Frecuencia: 5 kHz
#define PWM_BITS  10     // Resolución: 10 bits (0-1023)

// Establece el color RGB del LED mediante PWM
void setColor(uint16_t r, uint16_t g, uint16_t b) {
  
  ledcWrite(CH_R, r);
  ledcWrite(CH_G, g);
  ledcWrite(CH_B, b);
  Serial.printf("\nColor -> R:%4d  G:%4d  B:%4d\r\n", r, g, b);
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== Ejercicio 1: LED RGB con PWM (10 Bits) ===");

  // 1. Configurar los tres canales LEDC
  ledcSetup(CH_R, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_G, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_B, PWM_FREQ, PWM_BITS);

  // 2. Asignar cada canal a su pin GPIO
  ledcAttachPin(PIN_LED_R, CH_R);
  ledcAttachPin(PIN_LED_G, CH_G);
  ledcAttachPin(PIN_LED_B, CH_B);

  Serial.println("PWM configurado: 5 kHz, 10 bits, 3 canales");
}

void loop() {
  // --- Secuencia de colores básicos ---
  // Ahora el 100% de brillo equivale a 1023
  Serial.println("-- Secuencia de colores --");
  setColor(1023,    0,    0); delay(800);  // Rojo
  setColor(   0, 1023,    0); delay(800);  // Verde
  setColor(   0,    0, 1023); delay(800);  // Azul
  setColor(1023, 1023, 1023); delay(800);  // Blanco
  setColor(1023, 1023,    0); delay(800);  // Amarillo
  setColor(   0, 1023, 1023); delay(800);  // Cian
  setColor(1023,    0, 1023); delay(800);  // Magenta
  setColor(   0,    0,    0); delay(800);  // Apagado   

  // --- Efecto fade en canal rojo: 0 -> 1023 -> 0 ---
  Serial.println("-- Efecto fade rojo (Iniciando) --");

  // Fade In (Sube el brillo)
  for (int v = 0; v <= 1023; v += 1) {
    setColor(v, 0, 0); // Usamos ledcWrite directo para no saturar el Serial
    delay(100);
  }
  
  // Fade Out (Baja el brillo)
  for (int v = 1023; v >= 0; v -= 5) {
    setColor(v, 0, 0); 
    delay(100);
  }
  
  // Aseguramos que termine completamente apagado y mostramos estado final
  setColor(0, 0, 0);
  delay(1000);
}