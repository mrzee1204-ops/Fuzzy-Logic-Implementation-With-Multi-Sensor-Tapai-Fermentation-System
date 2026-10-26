#include <DHT.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

const int DHT_PIN     = 16;
const int FAN_PIN     = 18;
const int PELTIER_PIN = 19;

const int PWM_FREQ = 1000;
const int PWM_BITS = 10;
const int PWM_MAX  = 1023;

const float SETPOINT   = 35.0;
const float OVERHEAT   = 42.0;
const float COOL_AGAIN = 39.0;
const bool  AND_PRODUCT = true;
const unsigned long SENSOR_INTERVAL = 2000;
const unsigned long CONTROL_TIMEOUT = 6000;

DHT dht(DHT_PIN, DHT22);

float suhu = NAN;
float kelembapan = NAN;
float errorSuhu = NAN;
float tempMF[5];
float errorMF[5];
float sumW = 0;
float fanSum = 0;
float heaterSum = 0;
float fuzzyFan = NAN; 
float fuzzyHeater = NAN;

enum Mode { MODE_AUTO, MODE_MANUAL, MODE_STOP };
Mode mode = MODE_AUTO;
float manualFan = 0;
float manualPeltier = 0;
float fanOut = 0;
float heaterOut = 0;
bool suhuValid = false;
bool humidityValid = false;
bool tooHot = false;
bool pwmReady = false;
const char* controlState = "OUTPUT_OFF";
unsigned long lastSensor = 0;
unsigned long lastControl = 0;

const char* TEMP_NAME[5]  = {"Dingin", "AgakDingin", "Normal", "AgakPanas", "Panas"};
const char* ERROR_NAME[5] = {"NegBesar", "NegKecil", "Nol", "PosKecil", "PosBesar"};

float trapmf(float x, float a, float b, float c, float d) {
  if (!isfinite(x) || x < a || x > d) return 0;
  if (x >= b && x <= c) return 1;
  if (x < b) return (x - a) / (b - a);
  return (d - x) / (d - c);
}

float trimf(float x, float a, float b, float c) {
  if (!isfinite(x) || x <= a || x >= c) return 0;
  if (x == b) return 1;
  if (x < b) return (x - a) / (b - a);
  return (c - x) / (c - b);
}

void fuzzify(float temp, float error) {
  temp = constrain(temp, 28.0f, 42.0f);
  error = constrain(error, -7.0f, 7.0f);

  tempMF[0] = trapmf(temp, 28, 28, 30, 32.5);
  tempMF[1] = trimf(temp, 30, 32.5, 35);
  tempMF[2] = trimf(temp, 32.5, 35, 37.5);
  tempMF[3] = trimf(temp, 35, 37.5, 40);
  tempMF[4] = trapmf(temp, 37.5, 40, 42, 42);

  errorMF[0] = trapmf(error, -7, -7, -5, -2.5);
  errorMF[1] = trimf(error, -5, -2.5, 0);
  errorMF[2] = trimf(error, -2.5, 0, 2.5);
  errorMF[3] = trimf(error, 0, 2.5, 5);
  errorMF[4] = trapmf(error, 2.5, 5, 7, 7);
}

void addRule(int t, int e, float fanValue, float heaterValue) {
  float weight = tempMF[t] * errorMF[e];
  if (!AND_PRODUCT) weight = fminf(tempMF[t], errorMF[e]);

  sumW += weight;
  fanSum += weight * fanValue;
  heaterSum += weight * heaterValue;
}

void calculateFuzzy(float temp, float error) {
  fuzzyFan = NAN;
  fuzzyHeater = NAN;
  if (!isfinite(temp) || !isfinite(error)) return;

  fuzzify(temp, error);
  sumW = 0;
  fanSum = 0;
  heaterSum = 0;

  addRule(0, 0, 0, 100);
  addRule(0, 1, 0, 100);
  addRule(0, 2, 0, 75);
  addRule(0, 3, 0, 50);
  addRule(0, 4, 0, 25);

  addRule(1, 0, 0, 100);
  addRule(1, 1, 0, 75);
  addRule(1, 2, 0, 50);
  addRule(1, 3, 25, 25);
  addRule(1, 4, 25, 0);

  addRule(2, 0, 25, 75);
  addRule(2, 1, 25, 50);
  addRule(2, 2, 25, 25);
  addRule(2, 3, 50, 0);
  addRule(2, 4, 75, 0);

  addRule(3, 0, 50, 50);
  addRule(3, 1, 50, 25);
  addRule(3, 2, 50, 0);
  addRule(3, 3, 75, 0);
  addRule(3, 4, 100, 0);

  addRule(4, 0, 75, 25);
  addRule(4, 1, 75, 0);
  addRule(4, 2, 75, 0);
  addRule(4, 3, 100, 0);
  addRule(4, 4, 100, 0);

  if (sumW > 0) {
    fuzzyFan = fanSum / sumW;
    fuzzyHeater = heaterSum / sumW;
  }
}

void stopOutputs() {
  if (!pwmReady) return;
  ledcWrite(PELTIER_PIN, 0);
  ledcWrite(FAN_PIN, 0);
}

float clampDuty(float v) {
  if (!isfinite(v)) return 0;
  return constrain(v, 0.0f, 100.0f);
}

void applyOutputs() {
  if (mode == MODE_STOP) {
    fanOut = 0;
    heaterOut = 0;
    controlState = "STOP";
  } else {
    if (mode == MODE_AUTO) {
      fanOut = fuzzyFan;
      heaterOut = fuzzyHeater;
      controlState = "FUZZY";
    } else {
      fanOut = manualFan;
      heaterOut = manualPeltier;
      controlState = "MANUAL";
    }

    if (!suhuValid) {
      heaterOut = 0;
      if (mode == MODE_AUTO) fanOut = 0;
      controlState = "DHT_FAULT";
    } else if (mode == MODE_AUTO && (!isfinite(fuzzyFan) || !isfinite(fuzzyHeater))) {
      fanOut = 0;
      heaterOut = 0;
      controlState = "FUZZY_FAULT";
    } else {
      if (suhu >= OVERHEAT) tooHot = true;
      if (suhu <= COOL_AGAIN) tooHot = false;
      if (tooHot) {
        fanOut = 100;
        heaterOut = 0;
        controlState = "OVERHEAT";
      }
    }
  }

  fanOut = clampDuty(fanOut);
  heaterOut = clampDuty(heaterOut);
  lastControl = millis();

  if (!pwmReady) return;
  ledcWrite(PELTIER_PIN, lroundf(heaterOut * PWM_MAX / 100.0f));
  ledcWrite(FAN_PIN, lroundf(fanOut * PWM_MAX / 100.0f));
}

void controlCycle() {
  suhu = dht.readTemperature();
  kelembapan = dht.readHumidity();
  suhuValid = isfinite(suhu) && suhu >= -40 && suhu <= 80;
  humidityValid = isfinite(kelembapan) && kelembapan >= 0 && kelembapan <= 100;
  if (!suhuValid) suhu = NAN;
  if (!humidityValid) kelembapan = NAN;

  errorSuhu = suhu - SETPOINT;
  calculateFuzzy(suhu, errorSuhu);
  applyOutputs();

  Serial.printf("T=%.2f C RH=%.1f %% e=%.2f | ", suhu, kelembapan, errorSuhu);
  Serial.print("muT[");
  for (int i = 0; i < 5; i++) Serial.printf("%s=%.2f%s", TEMP_NAME[i], tempMF[i], i < 4 ? " " : "");
  Serial.print("] muE[");
  for (int i = 0; i < 5; i++) Serial.printf("%s=%.2f%s", ERROR_NAME[i], errorMF[i], i < 4 ? " " : "");
  Serial.printf("] sumW=%.3f | fuzzyFan=%.1f fuzzyPeltier=%.1f | Fan=%.1f%% Peltier=%.1f%% | %s\n",
                sumW, fuzzyFan, fuzzyHeater, fanOut, heaterOut, controlState);
}

void printHelp() {
  Serial.println("Perintah (akhiri Enter):");
  Serial.println("  auto   : kendali fuzzy otomatis dari DHT22 (bawaan)");
  Serial.println("  f<0-100>: kipas manual, contoh f50");
  Serial.println("  p<0-100>: Peltier manual, contoh p30");
  Serial.println("  stop   : matikan kipas dan Peltier");
  Serial.println("  help   : tampilkan bantuan");
  Serial.println("Proteksi suhu >= 42 C (kipas 100%, Peltier mati) tetap aktif di semua mode.");
}

void handleCommand(char* cmd) {
  for (char* c = cmd; *c; c++) *c = tolower((unsigned char)*c);
  if (!*cmd) return;

  if (strcmp(cmd, "auto") == 0) {
    mode = MODE_AUTO;
    Serial.println("Mode AUTO (fuzzy).");
  } else if (strcmp(cmd, "stop") == 0) {
    mode = MODE_STOP;
    Serial.println("Mode STOP.");
  } else if (strcmp(cmd, "help") == 0) {
    printHelp();
    return;
  } else if (cmd[0] == 'f' || cmd[0] == 'p') {
    char* end = nullptr;
    float v = strtof(cmd + 1, &end);
    if (end == cmd + 1 || *end != '\0' || v < 0 || v > 100) {
      Serial.println("Nilai harus 0-100, contoh: f50");
      return;
    }
    if (mode != MODE_MANUAL) { manualFan = 0; manualPeltier = 0; }
    mode = MODE_MANUAL;
    if (cmd[0] == 'f') manualFan = v; else manualPeltier = v;
    Serial.printf("Mode MANUAL: kipas=%.0f%% Peltier=%.0f%%\n", manualFan, manualPeltier);
  } else {
    Serial.println("Perintah tidak dikenal. Ketik help.");
    return;
  }
  applyOutputs();
}

void readSerial() {
  static char buf[16];
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      buf[len] = '\0';
      handleCommand(buf);
      len = 0;
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(PELTIER_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);
  digitalWrite(PELTIER_PIN, LOW);

  bool fanAttached = ledcAttach(FAN_PIN, PWM_FREQ, PWM_BITS);
  bool peltierAttached = ledcAttach(PELTIER_PIN, PWM_FREQ, PWM_BITS);
  pwmReady = fanAttached && peltierAttached;
  if (!pwmReady) Serial.println("PWM gagal dipasang. Keluaran dinonaktifkan.");
  stopOutputs();

  dht.begin();
  Serial.println("Pengujian aktuator: kipas DC + Peltier + fuzzy + DHT22");
  Serial.printf("Setpoint %.1f C | Overheat %.1f C | Pulih %.1f C\n", SETPOINT, OVERHEAT, COOL_AGAIN);
  printHelp();
  lastControl = millis();
}

void loop() {
  readSerial();

  unsigned long now = millis();
  if (now - lastSensor >= SENSOR_INTERVAL) {
    lastSensor = now;
    controlCycle();
  }

  if (now - lastControl > CONTROL_TIMEOUT) {
    fanOut = 0;
    heaterOut = 0;
    controlState = "TIMEOUT";
    stopOutputs();
  }
}
