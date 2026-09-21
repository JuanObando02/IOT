// ============================================================
// Ejercicio Integrado: LDR + DHT22 + HC-SR04 + MPU6050
// Práctica Capa de Dispositivos -- IoT USC
// ============================================================

#include <Arduino.h>
#include <DHT.h>
#include <Wire.h>
#include <math.h>

// ================= PINES Y CONFIGURACIÓN ====================

// --- Sensores Ejercicio 2 ---
#define PIN_LED_R  26
#define PIN_LED_G  27
#define PIN_LED_B  14
#define CH_R       0
#define CH_G       1
#define CH_B       2
#define PWM_FREQ   5000
#define PWM_BITS   8

#define PIN_LDR    34    // GPIO de entrada analógica exclusiva (ADC)
#define PIN_DHT    33    // DHT22 DATA (one-wire)
#define PIN_TRIG   25    // HC-SR04 trigger (salida)
#define PIN_ECHO   32    // HC-SR04 echo   (entrada)
#define DHT_TYPE   DHT22

DHT dht(PIN_DHT, DHT_TYPE);

// --- Sensor MPU6050 ---
#define I2C_SDA  21
#define I2C_SCL  22
#define MPU6050_ADDR         0x68
#define MPU6050_WHO_AM_I     0x75
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_GYRO_CONFIG  0x1B
#define MPU6050_ACCEL_CONFIG 0x1C
#define MPU6050_ACCEL_XOUT_H 0x3B

bool sensorConectado = false;

// ================= FUNCIONES AUXILIARES =====================

// --- Funciones Ejercicio 2 (LDR, LED, HC-SR04) ---
void setColor(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(CH_R, r);
  ledcWrite(CH_G, g);
  ledcWrite(CH_B, b);
}

void ajustarBrilloLED(int porcentaje) {
  porcentaje = constrain(porcentaje, 0, 100);
  uint8_t val = (uint8_t) map(porcentaje, 0, 100, 0, 255);
  setColor(val, val, val);
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

// --- Funciones MPU6050 ---
bool escribirRegistro(uint8_t reg, uint8_t dato) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.write(dato);
  return Wire.endTransmission() == 0;
}

bool leerRegistros(uint8_t reg, uint8_t *buf, int n) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)n);
  for (int i = 0; i < n; i++) {
    if (!Wire.available()) return false;
    buf[i] = Wire.read();
  }
  return true;
}

bool inicializarMPU6050() {
  uint8_t whoAmI;
  if (!leerRegistros(MPU6050_WHO_AM_I, &whoAmI, 1)) return false;
  Serial.printf("MPU6050 detectado. WHO_AM_I = 0x%02X\r\n", whoAmI);

  // 1. Reinicio completo
  escribirRegistro(MPU6050_PWR_MGMT_1, 0x80); 
  delay(100); 

  // 2. Despertar usando el reloj del eje X
  if (!escribirRegistro(MPU6050_PWR_MGMT_1, 0x01)) return false; 
  delay(100);
  
  // 3. NUEVO: Asegurar que PWR_MGMT_2 (0x6C) esté en 0x00 (Todos los ejes encendidos)
  if (!escribirRegistro(0x6C, 0x00)) return false;
  delay(50);
  
  if (!escribirRegistro(MPU6050_GYRO_CONFIG, 0x00)) return false;
  if (!escribirRegistro(MPU6050_ACCEL_CONFIG, 0x00)) return false;

  return (whoAmI == 0x68 || whoAmI == 0x72);
}

// ================= SETUP =====================

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== IoT USC ===");

  // 1. Configurar LED PWM
  ledcSetup(CH_R, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_G, PWM_FREQ, PWM_BITS);
  ledcSetup(CH_B, PWM_FREQ, PWM_BITS);
  ledcAttachPin(PIN_LED_R, CH_R);
  ledcAttachPin(PIN_LED_G, CH_G);
  ledcAttachPin(PIN_LED_B, CH_B);

  // 2. Configurar HC-SR04
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);

  // 3. Inicializar DHT22
  dht.begin();

  // 4. Inicializar I2C y MPU6050
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  if (inicializarMPU6050()) {
    sensorConectado = true;
    Serial.println("MPU6050 inicializado correctamente.");
  } else {
    Serial.println("ERROR: No se pudo inicializar el MPU6050.");
  }

  Serial.println("Estabilizando sensores... (2 segundos)");
  delay(2000); 
}

// ================= LOOP =====================

void loop() {
  Serial.println("\n------------------------------------------------");
  
  // --- LECTURA 1: LDR y LED ---
  int valorLDR = analogRead(PIN_LDR);
  int porcLuz  = map(valorLDR, 1600, 4095, 0, 100);
  ajustarBrilloLED(porcLuz);
  Serial.printf("LDR:    %4d ADC -> Brillo LED: %3d%%\r\n", valorLDR, porcLuz);

  // --- LECTURA 2: DHT22 ---
  float temperatura = dht.readTemperature();
  float humedad     = dht.readHumidity();
  if (!isnan(temperatura) && !isnan(humedad)) {
    Serial.printf("DHT22:  Temperatura = %.1f C  |  Humedad = %.1f %%\r\n", temperatura, humedad);
  } else {
    Serial.println("DHT22:  Error de lectura (NaN)");
  }

  // --- LECTURA 3: HC-SR04 ---
  float distancia = medirDistancia();
  if (distancia > 0) {
    Serial.printf("HC-SR04: Distancia = %.1f cm\r\n", distancia);
  } else {
    Serial.println("HC-SR04: Sin respuesta o fuera de rango");
  }

  // --- LECTURA 4: MPU6050 ---
  if (!sensorConectado) {
    Serial.println("MPU6050: Desconectado. Reintentando...");
    sensorConectado = inicializarMPU6050();
  } else {
    uint8_t data[14];
    if (leerRegistros(MPU6050_ACCEL_XOUT_H, data, 14)) {
      int16_t ax = (int16_t)((data[0]  << 8) | data[1]);
      int16_t ay = (int16_t)((data[2]  << 8) | data[3]);
      int16_t az = (int16_t)((data[4]  << 8) | data[5]);
      int16_t tp = (int16_t)((data[6]  << 8) | data[7]);
      int16_t gx = (int16_t)((data[8]  << 8) | data[9]);
      int16_t gy = (int16_t)((data[10] << 8) | data[11]);
      int16_t gz = (int16_t)((data[12] << 8) | data[13]);

      float axG  = ax / 16384.0f;
      float ayG  = ay / 16384.0f;
      float azG  = az / 16384.0f;
      float gxDs = gx / 131.0f;
      float gyDs = gy / 131.0f;
      float gzDs = gz / 131.0f;
      float tempC = (tp / 340.0f) + 36.53f;

      float pitch = atan2f(ayG, sqrtf(axG*axG + azG*azG)) * 180.0f / PI;
      float roll  = atan2f(-axG, sqrtf(ayG*ayG + azG*azG)) * 180.0f / PI;

      Serial.printf("MPU6050: Inclinacion -> Pitch: %5.1f deg | Roll: %5.1f deg\r\n", pitch, roll);
      Serial.printf("         Accel [g]    -> X:%6.2f Y:%6.2f Z:%6.2f\r\n", axG, ayG, azG);
      Serial.printf("         Gyro  [d/s]  -> X:%6.2f Y:%6.2f Z:%6.2f\r\n", gxDs, gyDs, gzDs);
      Serial.printf("         Temp Interna -> %.2f C\r\n", tempC);
    } else {
      Serial.println("MPU6050: Error leyendo datos");
    }
  }

  // Espera general de 2 segundos requerida por el DHT22
  delay(2000); 
}