#define BLYNK_TEMPLATE_ID "TMPL6jYJbFa9S"
#define BLYNK_TEMPLATE_NAME "Parameter Fermentasi Tapai"
#define BLYNK_AUTH_TOKEN    "38R-L9eycFiu3dnQeYvD56G-Cg08h12B"

#include <WiFi.h>
#include <BlynkSimpleEsp32_SSL.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_ADS1X15.h>
#include <LiquidCrystal_I2C.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Ticker.h>
#include <math.h>
#include <time.h>
#include "CloudRoots.h"
#include "Sample.h"
const char* ssid = "Kos Pria Tampan";
const char* password = "kosanpriatampan03";
const char* scriptURL = "https://script.google.com/macros/s/AKfycbxL4sivgPviYS3LSvXV4fOf6H6fPF6lFUjNGEi6opIFlrnQ9S_P2hfHALMiZV1iO5PUBw/exec";
const char* scriptToken = "1f24d92e664d4b278c3511d0b6282f4fe2a0cf0ebdb04cf6b5d6b8851dccf774";
const char* deviceId = "tapai-esp32-01";
const char* batchId = "tapai-001";

const int DHT_PIN = 16;
const int FAN_PIN = 18;
const int HEATER_PIN = 19;
const int SDA_PIN = 21;
const int SCL_PIN = 22;
const int ADS_ADDRESS = 0x48;
const int LCD_ADDRESS = 0x27;
const int PWM_FREQ = 1000;
const int PWM_BITS = 10;
const int PWM_MAX = 1023;
const float SETPOINT = 35.0;
const float OVERHEAT = 42.0;
const float COOL_AGAIN = 39.0;
const unsigned long SENSOR_INTERVAL = 2000;
const unsigned long SHEETS_INTERVAL = 30000;
const unsigned long CONTROL_TIMEOUT = 6000;
const unsigned long MQ_WARMUP = 300000;
const bool CONTROL_ENABLED = false;
const bool MQ3_CALIBRATED = false;
const bool PH_CALIBRATED = false;
const bool AND_PRODUCT = true;

const float MQ3_SLOPE = 0.0501;
const float MQ3_OFFSET = 0.008;
const float PH_SLOPE = -6.418;
const float PH_OFFSET = 17.226;
const float PH_CORRECTION = 0.0;
const float MQ135_RL = 2000.0;
const float MQ135_R0 = 12000.0;
const float MQ135_A = 110.47;
const float MQ135_B = -2.86;
const float SENSOR_SUPPLY = 5.0;
const float MQ3_SCALE = 1.0; 
const float MQ135_SCALE = 1.0;
const float PH_SCALE = 1.0;
const double ALCOHOL_THRESHOLD = 0.58;
const double PH_THRESHOLD = 4.0;
const int PH_SAMPLES = 20;

DHT dht(DHT_PIN, DHT22);
Adafruit_ADS1115 ads;
LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);
BlynkTimer timer;
Ticker outputWatchdog; 
Preferences memory; 

float suhu = NAN;
float kelembapan = NAN;
float alkohol = NAN;
float phValue = NAN;
float mq135Ppm = NAN;
float errorSuhu = NAN;
float voltageMQ3 = NAN;
float voltageMQ135 = NAN;
float voltagePH = NAN;
float analogMQ3 = NAN;
float analogMQ135 = NAN;
float analogPH = NAN;
float fanOut = 0;
float heaterOut = 0;
float fuzzyFan = NAN;
float fuzzyHeater = NAN;
float tempMF[5];
float errorMF[5];
float sumW = 0;
float fanSum = 0;
float heaterSum = 0;

bool suhuValid = false;
bool humidityValid = false;
bool mq3Valid = false;
bool phValid = false;
bool mq135Valid = false;
bool readyToCheck = false;
bool mature = false;
bool emailSent = false;
bool tooHot = false;
bool pwmReady = false;
bool adsReady = false;
bool lcdReady = false;
bool memoryReady = false;
bool mqWarmedUp = false;
String controlState = "OUTPUT_OFF";
String sampleId;
String pendingEmail;
int cloudStatus = 0;
unsigned long bootNumber = 0;
unsigned long sampleNumber = 0;
unsigned long missedLogs = 0;
unsigned long sampleTime = 0;
unsigned long lastControl = 0;
unsigned long lastAnalog = 0;
unsigned long lastMillis = 0;
uint64_t uptimeMs = 0;
float phBuffer[PH_SAMPLES] = {};
float phTotal = 0;
int phIndex = 0;
int phCount = 0;

// 2. MEMBERSHIP FUNCTION
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

  // Dingin: aturan 1-5.
  addRule(0, 0, 0, 100);
  addRule(0, 1, 0, 100);
  addRule(0, 2, 0, 75);
  addRule(0, 3, 0, 50);
  addRule(0, 4, 0, 25);

  // Agak dingin: aturan 6-10.
  addRule(1, 0, 0, 100);
  addRule(1, 1, 0, 75);
  addRule(1, 2, 0, 50);
  addRule(1, 3, 25, 25);
  addRule(1, 4, 25, 0);

  // Normal: aturan 11-15.
  addRule(2, 0, 25, 75);
  addRule(2, 1, 25, 50);
  addRule(2, 2, 25, 25);
  addRule(2, 3, 50, 0);
  addRule(2, 4, 75, 0);

  // Agak panas: aturan 16-20.
  addRule(3, 0, 50, 50);
  addRule(3, 1, 50, 25);
  addRule(3, 2, 50, 0);
  addRule(3, 3, 75, 0);
  addRule(3, 4, 100, 0);

  // Panas: aturan 21-25.
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
bool i2cAvailable(int address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

float readVoltage(int channel) {
  if (channel < 0 || channel > 2) return NAN;
  if (!adsReady || !i2cAvailable(ADS_ADDRESS)) return NAN;

  // Cara ini memberi batas waktu, sehingga ADS terlepas tidak menahan loop selamanya.
  const uint16_t inputs[3] = {
    ADS1X15_REG_CONFIG_MUX_SINGLE_0,
    ADS1X15_REG_CONFIG_MUX_SINGLE_1,
    ADS1X15_REG_CONFIG_MUX_SINGLE_2
  };
  ads.startADCReading(inputs[channel], false);
  unsigned long start = millis();
  while (!ads.conversionComplete()) {
    if (millis() - start >= 50) return NAN;
    delay(1);
  }
  float voltage = ads.computeVolts(ads.getLastConversionResults());
  if (!i2cAvailable(ADS_ADDRESS) || voltage < 0 || voltage > SENSOR_SUPPLY) return NAN;
  return voltage;
}

void readAnalog() {
  if (!adsReady && i2cAvailable(ADS_ADDRESS)) {
    adsReady = ads.begin(ADS_ADDRESS);
    ads.setGain(GAIN_TWOTHIRDS);
    ads.setDataRate(RATE_ADS1115_128SPS);
  }
  analogMQ135 = readVoltage(0) * MQ135_SCALE;
  analogMQ3 = readVoltage(1) * MQ3_SCALE;
  float newPH = readVoltage(2) * PH_SCALE;
  lastAnalog = millis();

  if (!isfinite(newPH)) {
    phCount = 0;
    phIndex = 0;
    phTotal = 0;
    analogPH = NAN;
    return;
  }
  if (phCount == PH_SAMPLES) phTotal -= phBuffer[phIndex];
  else phCount++;

  phBuffer[phIndex] = newPH;
  phTotal += newPH;
  phIndex = (phIndex + 1) % PH_SAMPLES;
  analogPH = phTotal / phCount;
}

void readAlkoholAndPH() {
  // Tegangan dan hasil konversinya diambil pada pembaruan kontrol yang sama.
  voltageMQ3 = analogMQ3;
  voltageMQ135 = analogMQ135;
  voltagePH = analogPH;
  if (millis() - lastAnalog >= 500) {
    voltageMQ3 = NAN;
    voltageMQ135 = NAN;
    voltagePH = NAN;
  }
  alkohol = MQ3_SLOPE * voltageMQ3 + MQ3_OFFSET;
  phValue = PH_SLOPE * voltagePH + PH_OFFSET + PH_CORRECTION;
  mq135Ppm = NAN;

  if (voltageMQ135 > 0 && voltageMQ135 < SENSOR_SUPPLY) {
    float rs = MQ135_RL * (SENSOR_SUPPLY / voltageMQ135 - 1);
    mq135Ppm = MQ135_A * powf(rs / MQ135_R0, MQ135_B);
  }
  mq3Valid = isfinite(alkohol) && voltageMQ3 > 0.01f && voltageMQ3 < SENSOR_SUPPLY - 0.01f;
  phValid = isfinite(phValue) && phCount == PH_SAMPLES && phValue >= 0 && phValue <= 14;
  mq135Valid = isfinite(mq135Ppm) && mq135Ppm >= 10 && mq135Ppm <= 1000;
  if (!mq135Valid) mq135Ppm = NAN;

  if (uptimeMs >= MQ_WARMUP) mqWarmedUp = true;
  readyToCheck = mq3Valid && phValid && mqWarmedUp && MQ3_CALIBRATED && PH_CALIBRATED;
}
void stopOutputs() {
  ledcWrite(HEATER_PIN, 0);
  ledcWrite(FAN_PIN, 0);
}

void applyOutputs() {
  fanOut = fuzzyFan;
  heaterOut = fuzzyHeater;
  controlState = "FUZZY";

  if (!CONTROL_ENABLED || !pwmReady) {
    fanOut = 0;
    heaterOut = 0;
    controlState = "OUTPUT_OFF";
  } else if (!suhuValid || !isfinite(fuzzyFan) || !isfinite(fuzzyHeater)) {
    fanOut = 0;
    heaterOut = 0;
    controlState = "DHT_FAULT";
  } else {
    if (suhu >= OVERHEAT) tooHot = true;
    if (suhu <= COOL_AGAIN) tooHot = false;
    if (tooHot) {
      fanOut = 100;
      heaterOut = 0;
      controlState = "OVERHEAT";
    }
  }
  if (!pwmReady) return;

  outputWatchdog.once_ms(CONTROL_TIMEOUT, stopOutputs);
  int heaterPWM = lroundf(heaterOut * PWM_MAX / 100.0f);
  int fanPWM = lroundf(fanOut * PWM_MAX / 100.0f);
  ledcWrite(HEATER_PIN, heaterPWM);
  ledcWrite(FAN_PIN, fanPWM);
}

void loopControl() {
  suhu = dht.readTemperature();
  kelembapan = dht.readHumidity();
  suhuValid = isfinite(suhu) && suhu >= -40 && suhu <= 80;
  humidityValid = isfinite(kelembapan) && kelembapan >= 0 && kelembapan <= 100;
  if (!suhuValid) suhu = NAN;
  if (!humidityValid) kelembapan = NAN;

  readAlkoholAndPH();
  errorSuhu = suhu - SETPOINT;
  calculateFuzzy(suhu, errorSuhu);
  applyOutputs();
  lastControl = millis();

  sampleNumber++;
  sampleId = String(deviceId) + "-" + batchId + "-" + bootNumber + "-" + sampleNumber;
  sampleTime = time(nullptr);
  if (sampleTime < 1700000000UL) sampleTime = 0;
  checkMaturity();
  showLCD();

  Serial.printf("T=%.2f RH=%.1f pH=%.2f A=%.4f%% MQ=%.1f F=%.1f H=%.1f\n",
                suhu, kelembapan, phValue, alkohol, mq135Ppm, fanOut, heaterOut);
}
void showLCD() {
  if (!lcdReady || !i2cAvailable(LCD_ADDRESS)) return;
  char top[40];
  char bottom[40];
  int page = (millis() / 4000) % 4;

  if (page == 0) {
    snprintf(top, sizeof(top), "T:%.1fC SP:35", suhu);
    snprintf(bottom, sizeof(bottom), "RH:%.1f%%", kelembapan);
  } else if (page == 1) {
    snprintf(top, sizeof(top), "Alc:%.3f%% EST", alkohol);
    snprintf(bottom, sizeof(bottom), "pH:%.2f", phValue);
  } else if (page == 2) {
    snprintf(top, sizeof(top), "MQ:%.0f estppm", mq135Ppm);
    snprintf(bottom, sizeof(bottom), "Fan:%.0f PTC:%.0f", fanOut, heaterOut);
  } else {
    snprintf(top, sizeof(top), "%s", controlState.c_str());
    if (mature) snprintf(bottom, sizeof(bottom), "MATANG (AMBANG)");
    else if (readyToCheck) snprintf(bottom, sizeof(bottom), "FERMENTASI");
    else snprintf(bottom, sizeof(bottom), "CEK KAL/WARMUP");
  }
  lcd.setCursor(0, 0);
  lcd.print("                ");
  lcd.setCursor(0, 0);
  lcd.print(String(top).substring(0, 16));
  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);
  lcd.print(String(bottom).substring(0, 16));
}
void sendToBlynk() {
  static int pin = 0;
  if (!Blynk.connected() || sampleNumber == 0) return;

  switch (pin) {
    case 0:
      if (suhuValid) Blynk.virtualWrite(V0, suhu);
      break;
    case 1:
      if (humidityValid) Blynk.virtualWrite(V1, kelembapan);
      break;
    case 2:
      if (mq3Valid) Blynk.virtualWrite(V2, alkohol);
      break;
    case 3:
      if (mq135Valid) Blynk.virtualWrite(V3, mq135Ppm);
      break;
    case 4:
      if (phValid) Blynk.virtualWrite(V4, phValue);
      break;
    case 5:
      Blynk.virtualWrite(V5, fanOut);
      break;
    case 6:
      Blynk.virtualWrite(V6, heaterOut);
      break;
  }
  pin = (pin + 1) % 7;
}
String makePayload(bool maturityEvent) {
  JsonDocument data;
  data["api_token"] = scriptToken;
  data["schema"] = 1;
  data["sample_id"] = sampleId;
  data["device_id"] = deviceId;
  data["batch_id"] = batchId;
  data["kind"] = maturityEvent ? "maturity" : "telemetry";
  data["boot"] = bootNumber;
  data["sequence"] = sampleNumber;
  data["uptime_ms"] = uptimeMs;
  data["sample_epoch"] = sampleTime;
  data["temperature_c"] = suhu;
  data["humidity_pct"] = kelembapan;
  data["error_c"] = errorSuhu;
  data["alcohol_est_pct"] = alkohol;
  data["ph"] = phValue;
  data["mq135_est_ppm"] = mq135Ppm;
  data["mq3_v"] = voltageMQ3;
  data["mq135_v"] = voltageMQ135;
  data["ph_v"] = voltagePH;
  data["fan_pct"] = fanOut;
  data["ptc_pct"] = heaterOut;
  data["fuzzy_fan_pct"] = fuzzyFan;
  data["fuzzy_ptc_pct"] = fuzzyHeater;
  data["temperature_valid"] = suhuValid;
  data["humidity_valid"] = humidityValid;
  data["mq3_valid"] = mq3Valid;
  data["ph_valid"] = phValid;
  data["mq135_valid"] = mq135Valid;
  data["maturity_eligible"] = readyToCheck;
  data["mature_latched"] = mature;
  data["mq3_calibrated"] = MQ3_CALIBRATED;
  data["ph_calibrated"] = PH_CALIBRATED;
  data["and_method"] = AND_PRODUCT ? "prod" : "min";
  data["control_state"] = controlState;
  data["queue_drops"] = missedLogs;

  String body;
  serializeJson(data, body); 
  return body;
}
bool postToAppsScript(String body) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (time(nullptr) < 1700000000) { cloudStatus = -2; return false; }

  String url = scriptURL;
  if (!url.startsWith("https://script.google.com/macros/s/") || !url.endsWith("/exec")) {
    cloudStatus = -3;
    return false;
  }
  JsonDocument request;
  if (deserializeJson(request, body)) return false;
  request["api_token"] = scriptToken;
  String expectedId = request["sample_id"].as<String>();
  bool isEvent = request["kind"] == "maturity";
  body = "";
  serializeJson(request, body);
  WiFiClientSecure client;
  client.setCACert(CLOUD_ROOTS);
  client.setHandshakeTimeout(15);
  for (int hop = 0; hop < 4; hop++) {
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(4000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(client, url)) { cloudStatus = -3; return false; }
    const char* headers[] = {"Location", "Content-Type"};
    http.collectHeaders(headers, 2);
    int status;
    if (hop == 0) {
      http.addHeader("Content-Type", "application/json");
      status = http.POST(body);
    } else {
      status = http.GET();
    }
    cloudStatus = status;
    if (status == 302 || status == 303) {
      url = http.header("Location");
      http.end();
      if (!url.startsWith("https://script.googleusercontent.com/")) {
        cloudStatus = -4;
        return false;
      }
      continue;
    }
    if (status != 200) { http.end(); return false; }
    if (!http.header("Content-Type").startsWith("application/json") || http.getSize() > 4096) {
      http.end(); cloudStatus = -5; return false;
    }
    String response = http.getString();
    http.end();
    JsonDocument reply;
    if (deserializeJson(reply, response)) { cloudStatus = -5; return false; }
    if (reply["ok"] != true) {
      Serial.println(reply["message"].as<String>());
      cloudStatus = reply["code"] | 503;
      return false;
    }
    if (reply["sample_id"].as<String>() != expectedId || (isEvent && reply["email_sent"] != true)) {
      cloudStatus = -6;
      return false;
    }
    cloudStatus = 200;
    return true;
  }
  cloudStatus = -4;
  return false;
}
void sendToGoogleSheets() {
  if (sampleNumber == 0 || !memoryReady) return;
  if (!postToAppsScript(makePayload(false))) {
    missedLogs++;
    Serial.printf("Kirim ke Google Sheets gagal (cloudStatus=%d), missedLogs=%lu\n",
                  cloudStatus, missedLogs);
  }
}
void checkMaturity() {
  if (mature || !readyToCheck) return;
  if (!(alkohol > ALCOHOL_THRESHOLD && phValue < PH_THRESHOLD && phValue >= 0)) return;

  mature = true;
  pendingEmail = makePayload(true);
  JsonDocument event;
  deserializeJson(event, pendingEmail);
  event.remove("api_token");
  pendingEmail = "";
  serializeJson(event, pendingEmail);
  if (memoryReady && memory.putString("event-json", pendingEmail) != pendingEmail.length()) {
    Serial.println("Event masih di RAM; penyimpanan NVS gagal.");
  }
  Serial.println("Ambang matang tercapai; email menunggu pengiriman.");
}

void sendMaturityNotification() {
  if (pendingEmail.isEmpty() || emailSent || !memoryReady) return;
  if (postToAppsScript(pendingEmail)) {
    emailSent = true;
    memory.putBool("mailack", true);
    memory.remove("event-json");
    pendingEmail = "";
  }
}
String restoreOldNotification(Preferences& storage, const char* device, const char* batch) {
  if (!storage.isKey("mature")) return "";
  if (storage.getType("mature") != PT_BLOB ||
      storage.getBytesLength("mature") != sizeof(Sample)) {
    Serial.println("Notifikasi lama: format NVS tidak cocok dengan Sample.h; data dibiarkan.");
    return "";
  }
  Sample previous;
  if (storage.getBytes("mature", &previous, sizeof(previous)) != sizeof(previous)) {
    Serial.println("Notifikasi lama: snapshot NVS gagal dibaca.");
    return "";
  }
  if (previous.schema != 2 || !previous.mature) {
    Serial.println("Notifikasi lama: schema/status snapshot tidak cocok; data dibiarkan.");
    return "";
  }
  previous.state[sizeof(previous.state) - 1] = '\0';
  JsonDocument event;
  event["schema"] = 1;
  event["sample_id"] = String(device) + "-" + batch + "-" + previous.boot + "-" + previous.sequence;
  event["device_id"] = device;
  event["batch_id"] = batch;
  event["kind"] = "maturity";
  event["boot"] = previous.boot;
  event["sequence"] = previous.sequence;
  event["uptime_ms"] = previous.uptimeMs;
  event["sample_epoch"] = previous.epoch;
  event["temperature_c"] = previous.temperature;
  event["humidity_pct"] = previous.humidity;
  event["error_c"] = previous.error;
  event["alcohol_est_pct"] = previous.alcohol;
  event["ph"] = previous.ph;
  event["mq135_est_ppm"] = previous.mq135Ppm;
  event["mq3_v"] = previous.mq3V;
  event["mq135_v"] = previous.mq135V;
  event["ph_v"] = previous.phV;
  event["fan_pct"] = previous.fan;
  event["ptc_pct"] = previous.heater;
  event["fuzzy_fan_pct"] = previous.rawFan;
  event["fuzzy_ptc_pct"] = previous.rawHeater;
  event["temperature_valid"] = previous.temperatureValid;
  event["humidity_valid"] = previous.humidityValid;
  event["mq3_valid"] = previous.mq3Valid;
  event["ph_valid"] = previous.phValid;
  event["mq135_valid"] = previous.mq135Valid;
  event["maturity_eligible"] = previous.eligible;
  event["mature_latched"] = previous.mature;
  event["mq3_calibrated"] = previous.mq3Calibrated;
  event["ph_calibrated"] = previous.phCalibrated;
  event["and_method"] = previous.andProduct ? "prod" : "min";
  event["control_state"] = previous.state;
  event["queue_drops"] = previous.drops;
  String body;
  serializeJson(event, body);
  return body;
}

void setupMemory() {
  memoryReady = memory.begin("tapai-v1", false);
  if (!memoryReady) return;

  bootNumber = memory.getUInt("boot", 0) + 1;
  memoryReady = memory.putUInt("boot", bootNumber) == sizeof(uint32_t);
  if (!memoryReady) return;
  String batchKey = String(deviceId) + "/" + batchId;
  if (memory.getString("batch", "") != batchKey) {
    memory.remove("mature");
    memory.remove("event-json");
    memory.putBool("mailack", false);
    memory.putString("batch", batchKey);
  }
  pendingEmail = memory.getString("event-json", "");
  emailSent = memory.getBool("mailack", false);
  if (!emailSent && pendingEmail.isEmpty()) {
    pendingEmail = restoreOldNotification(memory, deviceId, batchId);
    if (!pendingEmail.isEmpty()) memory.putString("event-json", pendingEmail);
  }
  mature = emailSent || !pendingEmail.isEmpty();
}

void maintainConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    return;
  }
  if (time(nullptr) < 1700000000) {
    Serial.println("Waktu NTP belum tersinkron, mencoba ulang...");
    configTime(0, 0, "time.google.com", "pool.ntp.org");
    return;
  }
  if (!Blynk.connected()) Blynk.connect(1000);
}

void setup() {
  Serial.begin(115200);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(HEATER_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);
  digitalWrite(HEATER_PIN, LOW);
  bool fanAttached = ledcAttach(FAN_PIN, PWM_FREQ, PWM_BITS);
  bool heaterAttached = ledcAttach(HEATER_PIN, PWM_FREQ, PWM_BITS);
  pwmReady = fanAttached && heaterAttached;
  stopOutputs();

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  Wire.setTimeOut(20);
  lcdReady = i2cAvailable(LCD_ADDRESS);
  if (lcdReady) { lcd.init(); lcd.backlight(); lcd.clear(); }
  adsReady = ads.begin(ADS_ADDRESS);
  ads.setGain(GAIN_TWOTHIRDS);  // +/-6,144 V; AO tetap tidak boleh melebihi VDD ADS.
  ads.setDataRate(RATE_ADS1115_128SPS);
  dht.begin();
  setupMemory();
  if (!adsReady) Serial.println("ADS1115 tidak terdeteksi. Periksa I2C.");
  if (!memoryReady) Serial.println("NVS gagal. Pengiriman Sheet/email dinonaktifkan.");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  configTime(0, 0, "time.google.com", "pool.ntp.org");
  Blynk.config(BLYNK_AUTH_TOKEN);

  timer.setInterval(100L, readAnalog);
  timer.setInterval(SENSOR_INTERVAL, loopControl);
  timer.setInterval(250L, sendToBlynk);
  timer.setInterval(SHEETS_INTERVAL, sendToGoogleSheets);
  timer.setInterval(30000L, sendMaturityNotification);
  timer.setInterval(15000L, maintainConnection);
}
void loop() {
  unsigned long now = millis();
  uptimeMs += (uint32_t)(now - lastMillis);
  lastMillis = now;
  if (sampleNumber > 0 && now - lastControl > CONTROL_TIMEOUT) {
    fanOut = 0;
    heaterOut = 0;
    suhuValid = false;
    controlState = "DHT_FAULT";
  }
  if (WiFi.status() == WL_CONNECTED && Blynk.connected()) Blynk.run();
  timer.run();
}