#include <DHT.h>
#include <math.h>

// ─── DHT22 ───────────────────────────────────
#define DHTPIN  4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// ─── Output PWM ──────────────────────────────
#define FAN_PIN    18
#define HEATER_PIN 19
#define PWM_FREQ   5000
#define PWM_RES    8

// ─── Setpoint ────────────────────────────────
const float setpoint = 35;

// ─── Membership Function Arrays ──────────────
#define MF_SIZE 5
float tempMF[MF_SIZE];
float errorMF[MF_SIZE];

// ═══════════════════════════════════════════════
//  FUNGSI FUZZY
// ═══════════════════════════════════════════════

float trapmf(float x, float a, float b, float c, float d) {
  float val1 = (x - a) / (b - a);
  float val2 = (d - x) / (d - c);
  float min1 = val1 < 1.0f ? val1 : 1.0f;
  float min2 = min1 < val2 ? min1 : val2;
  return min2 > 0.0f ? min2 : 0.0f;
}

float trimf(float x, float a, float b, float c) {
  if (x <= a || x >= c) return 0.0f;
  else if (x == b)      return 1.0f;
  else if (x < b)       return (x - a) / (b - a);
  else                  return (c - x) / (c - b);
}

void fuzzify(float temp, float error) {
  tempMF[0] = trapmf(temp, 28, 28, 30, 32.5);
  tempMF[1] = trimf(temp,  30, 32.5, 35);
  tempMF[2] = trimf(temp,  32.5, 35, 37.5);
  tempMF[3] = trimf(temp,  35, 37.5, 40);
  tempMF[4] = trapmf(temp, 37.5, 40, 42, 42);

  error = constrain(error, -5, 5);
  errorMF[0] = trapmf(error, -7, -7, -5, -2.5);
  errorMF[1] = trimf(error,  -5, -2.5,  0);
  errorMF[2] = trimf(error,  -2.5,  0,  2.5);
  errorMF[3] = trimf(error,   0,  2.5,  5);
  errorMF[4] = trapmf(error,  2.5,  5,  7,  7);
}

// ═══════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  dht.begin();

  // ── PWM API Baru (ESP32 Arduino v3.x) ────────
  // Tidak perlu ledcSetup dan ledcAttachPin lagi
  // Cukup ledcAttach(pin, freq, resolution)
  ledcAttach(HEATER_PIN, PWM_FREQ, PWM_RES);
  ledcAttach(FAN_PIN,    PWM_FREQ, PWM_RES);

  Serial.println("=========================================");
  Serial.println("  Sistem Fuzzy Kontrol Suhu — DHT22");
  Serial.println("  Setpoint : " + String(setpoint) + " C");
  Serial.println("=========================================");
  Serial.println("Suhu(C) | Hum(%) | Error | Heater(%) | Fan(%)");
  Serial.println("-------------------------------------------------");
}

// ═══════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════

void loop() {

  // ── Baca DHT22 ───────────────────────────────
  float temp       = dht.readTemperature();
  float kelembaban = dht.readHumidity();

  if (isnan(temp) || isnan(kelembaban)) {
    Serial.println("[ERROR] Gagal membaca DHT22!");
    delay(2000);
    return;
  }

  float error = constrain(setpoint - temp, -5, 5);

  // ── Fuzzifikasi ───────────────────────────────
  fuzzify(temp, error);

  // ── Inferensi 14 Rules ────────────────────────
  float sumW = 0, heaterOut = 0, fanOut = 0;

  { float w = fmin(tempMF[0], errorMF[4]); heaterOut += w * 100; fanOut += w * 20;  sumW += w; } // RK1
  { float w = fmin(tempMF[0], errorMF[3]); heaterOut += w * 80;  fanOut += w * 20;  sumW += w; } // RK2
  { float w = fmin(tempMF[0], errorMF[2]); heaterOut += w * 60;  fanOut += w * 20;  sumW += w; } // RK3
  { float w = fmin(tempMF[1], errorMF[4]); heaterOut += w * 80;  fanOut += w * 20;  sumW += w; } // RK4
  { float w = fmin(tempMF[1], errorMF[3]); heaterOut += w * 60;  fanOut += w * 20;  sumW += w; } // RK5
  { float w = fmin(tempMF[1], errorMF[2]); heaterOut += w * 40;  fanOut += w * 20;  sumW += w; } // RK6
  { float w = fmin(tempMF[2], errorMF[3]); heaterOut += w * 40;  fanOut += w * 20;  sumW += w; } // RK7
  { float w = fmin(tempMF[2], errorMF[2]); heaterOut += w * 20;  fanOut += w * 20;  sumW += w; } // RK8
  { float w = fmin(tempMF[2], errorMF[1]); heaterOut += w * 20;  fanOut += w * 40;  sumW += w; } // RK9
  { float w = fmin(tempMF[3], errorMF[2]); heaterOut += w * 20;  fanOut += w * 40;  sumW += w; } // RK10
  { float w = fmin(tempMF[3], errorMF[1]); heaterOut += w * 20;  fanOut += w * 60;  sumW += w; } // RK11
  { float w = fmin(tempMF[3], errorMF[0]); heaterOut += w * 20;  fanOut += w * 80;  sumW += w; } // RK12
  { float w = fmin(tempMF[4], errorMF[1]); heaterOut += w * 20;  fanOut += w * 80;  sumW += w; } // RK13
  { float w = fmin(tempMF[4], errorMF[0]); heaterOut += w * 20;  fanOut += w * 100; sumW += w; } // RK14

  // ── Defuzzifikasi ─────────────────────────────
  if (sumW > 0) {
    heaterOut /= sumW;
    fanOut    /= sumW;
  } else {
    heaterOut = 0;
    fanOut    = 0;
  }

  // ── Konversi ke PWM ───────────────────────────
  uint8_t heaterPWM = map(heaterOut, 0, 100, 0, 255);
  uint8_t fanPWM    = map(fanOut,    0, 100, 0, 255);

  // ── API Baru: ledcWrite(pin, dutyCycle) ───────
  ledcWrite(HEATER_PIN, heaterPWM);
  ledcWrite(FAN_PIN,    fanPWM);

  // ── Serial Monitor ────────────────────────────
  Serial.printf("%.2f C  | %.1f %%  | %+.2f  | %6.1f %%  | %6.1f %%\n",
                temp, kelembaban, error, heaterOut, fanOut);

  delay(1000);
}