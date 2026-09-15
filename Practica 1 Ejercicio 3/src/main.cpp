// ============================================================
// Ejercicio 3: Comunicación I2C con MPU6050
// Práctica Capa de Dispositivos -- IoT USC
// Adaptado de practica2_sensor_mpu6050.c (sin FreeRTOS)
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

// Pines I2C del ESP32
#define I2C_SDA  21
#define I2C_SCL  22

// Dirección I2C y registros del MPU6050
#define MPU6050_ADDR         0x68
#define MPU6050_WHO_AM_I     0x75
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_GYRO_CONFIG  0x1B
#define MPU6050_ACCEL_CONFIG 0x1C
#define MPU6050_ACCEL_XOUT_H 0x3B

bool sensorConectado = false;

// ---- Funciones I2C ----

// Escribe un byte en el registro 'reg' del MPU6050
bool escribirRegistro(uint8_t reg, uint8_t dato) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.write(dato);
  return Wire.endTransmission() == 0;
}

// Lee 'n' bytes consecutivos desde 'reg' hacia 'buf'
// Usa repeated-start (endTransmission(false)) para lectura I2C estándar
bool leerRegistros(uint8_t reg, uint8_t *buf, int n) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;  // repeated start
  Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)n);
  for (int i = 0; i < n; i++) {
    if (!Wire.available()) return false;
    buf[i] = Wire.read();
  }
  return true;
}

// Inicializa el MPU6050: verifica WHO_AM_I, sale de sleep,
// configura escalas de giroscopio y acelerómetro
bool inicializarMPU6050() {
  uint8_t whoAmI;
  if (!leerRegistros(MPU6050_WHO_AM_I, &whoAmI, 1)) return false;
  Serial.printf("WHO_AM_I = 0x%02X (esperado 0x68)\r\n", whoAmI);

  // Despertar el sensor: escribir 0x00 en PWR_MGMT_1 (borra el bit SLEEP)
  if (!escribirRegistro(MPU6050_PWR_MGMT_1, 0x00)) return false;
  delay(100);

  // Giroscopio: escala ±250 °/s -> factor de conversión 131 LSB/(°/s)
  if (!escribirRegistro(MPU6050_GYRO_CONFIG, 0x00)) return false;

  // Acelerómetro: escala ±2 g -> factor de conversión 16384 LSB/g
  if (!escribirRegistro(MPU6050_ACCEL_CONFIG, 0x00)) return false;

  return (whoAmI == 0x68);
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== Ejercicio 3: MPU6050 via I2C ===");

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);  // 400 kHz (modo rápido)

  if (inicializarMPU6050()) {
    sensorConectado = true;
    Serial.println("MPU6050 inicializado correctamente");
  } else {
    Serial.println("ERROR: No se pudo inicializar el MPU6050");
    Serial.println("Verifica: SDA->GPIO21, SCL->GPIO22, AD0->GND, VCC->3V3");
  }
}

void loop() {
  if (!sensorConectado) {
    Serial.println("Sensor no disponible. Reintentando inicializacion...");
    sensorConectado = inicializarMPU6050();
    delay(2000);
    return;
  }

  // Lectura en rafaga: 14 bytes desde 0x3B
  //   bytes  0-5  : Accel X, Y, Z  (2 bytes cada uno, big-endian)
  //   bytes  6-7  : Temperatura     (2 bytes)
  //   bytes  8-13 : Gyro  X, Y, Z  (2 bytes cada uno, big-endian)
  uint8_t data[14];
  if (!leerRegistros(MPU6050_ACCEL_XOUT_H, data, 14)) {
    Serial.println("Error leyendo datos del MPU6050");
    delay(2000);
    return;
  }

  // Combinar bytes (big-endian) -> enteros de 16 bits con signo
  int16_t ax = (int16_t)((data[0]  << 8) | data[1]);
  int16_t ay = (int16_t)((data[2]  << 8) | data[3]);
  int16_t az = (int16_t)((data[4]  << 8) | data[5]);
  int16_t tp = (int16_t)((data[6]  << 8) | data[7]);
  int16_t gx = (int16_t)((data[8]  << 8) | data[9]);
  int16_t gy = (int16_t)((data[10] << 8) | data[11]);
  int16_t gz = (int16_t)((data[12] << 8) | data[13]);

  // Convertir a unidades físicas
  float axG  = ax / 16384.0f;   // ±2 g    -> 16384 LSB/g
  float ayG  = ay / 16384.0f;
  float azG  = az / 16384.0f;
  float gxDs = gx / 131.0f;     // ±250°/s -> 131 LSB/(°/s)
  float gyDs = gy / 131.0f;
  float gzDs = gz / 131.0f;
  float tempC = (tp / 340.0f) + 36.53f;  // fórmula del datasheet MPU6050

  // Calcular ángulos de inclinación con atan2
  float pitch = atan2f(ayG, sqrtf(axG*axG + azG*azG)) * 180.0f / PI;
  float roll  = atan2f(-axG, sqrtf(ayG*ayG + azG*azG)) * 180.0f / PI;

  // Mostrar resultados
  Serial.println("=== MPU6050 ===");
  Serial.printf("Accel [g]   -> X:%7.3f  Y:%7.3f  Z:%7.3f\r\n", axG, ayG, azG);
  Serial.printf("Gyro  [d/s] -> X:%7.2f  Y:%7.2f  Z:%7.2f\r\n", gxDs, gyDs, gzDs);
  Serial.printf("Temp interna:  %.2f C\n", tempC);
  Serial.printf("Pitch: %6.1f deg   Roll: %6.1f deg\r\n\n", pitch, roll);

  delay(500);
}