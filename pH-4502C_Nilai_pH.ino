#define BLYNK_TEMPLATE_ID   "TMPL6RoDzw7vu"
#define BLYNK_TEMPLATE_NAME "Potential Hydrogen"
#define BLYNK_AUTH_TOKEN    "O4kjZvgf_Ev_dNcVOGgAcHn_oCsJKRde"

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <BlynkSimpleEsp32_SSL.h>

const char* ssid     = "Kos Pria Tampan";
const char* password = "kosanpriatampan03";

const int     SDA_PIN     = 21;
const int     SCL_PIN     = 22;
const uint8_t ADS_ADDRESS = 0x48;
const uint8_t PH_CHANNEL  = 1;

const float PH_SCALE      = 1.0;
const float PH_MAX_VOLT   = 5.0;
const float PH_SLOPE      = -6.418;
const float PH_OFFSET     = 17.226;
const float PH_CORRECTION = 0.00; 
const int PH_SAMPLES = 20;
float phBuffer[PH_SAMPLES] = {};
float phTotal = 0;
int   phIndex = 0;
int   phCount = 0;

const unsigned long SAMPLE_INTERVAL = 200;
const unsigned long SEND_INTERVAL   = 1000;
const unsigned long LINK_INTERVAL   = 5000;

#define VPIN_PH V4

Adafruit_ADS1115 ads;
BlynkTimer timer;
bool adsReady = false;
float lastVoltage = NAN;

bool i2cAvailable() {
  Wire.beginTransmission(ADS_ADDRESS);
  return Wire.endTransmission() == 0;
}

bool startADS() {
  if (!i2cAvailable()) return false;
  if (!ads.begin(ADS_ADDRESS)) return false;
  ads.setGain(GAIN_TWOTHIRDS);
  ads.setDataRate(RATE_ADS1115_128SPS);
  return true;
}

void resetBuffer() {
  phTotal = 0; phIndex = 0; phCount = 0;
  for (int i = 0; i < PH_SAMPLES; i++) phBuffer[i] = 0;
}
float readPHVoltage() {
  if (!adsReady || !i2cAvailable()) { adsReady = false; return NAN; }
  int16_t raw = ads.readADC_SingleEnded(PH_CHANNEL);
  if (!i2cAvailable()) { adsReady = false; return NAN; }
  float volts = ads.computeVolts(raw) * PH_SCALE;
  if (volts <= 0.01f || volts >= PH_MAX_VOLT) return NAN;
  return volts;
}

void takeSample() {
  float volts = readPHVoltage();
  if (isnan(volts)) {
    resetBuffer();
    lastVoltage = NAN;
    return;
  }
  if (phCount == PH_SAMPLES) phTotal -= phBuffer[phIndex];
  else phCount++;
  phBuffer[phIndex] = volts;
  phTotal += volts;
  phIndex = (phIndex + 1) % PH_SAMPLES;
  lastVoltage = volts;
}

void sendPH() {
  if (phCount < PH_SAMPLES) {
    Serial.printf("Mengumpulkan sampel pH... (%d/%d) atau sensor tidak valid. Cek ADS1115 dan kabel ke A1.\n",
                  phCount, PH_SAMPLES);
    return;
  }

  float voltage = phTotal / phCount;
  float pH = PH_SLOPE * voltage + PH_OFFSET + PH_CORRECTION;

  if (!isfinite(pH) || pH < 0 || pH > 14) {
    Serial.printf("Voltage: %.3f V | pH di luar rentang (%.2f)\n", voltage, pH);
    return;
  }

  Serial.printf("Voltage: %.3f V | pH: %.2f | Blynk: %s\n",
                voltage, pH, Blynk.connected() ? "terhubung" : "putus");

  if (Blynk.connected()) Blynk.virtualWrite(VPIN_PH, pH);
}

void keepLinks() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
  } else if (!Blynk.connected()) {
    Blynk.connect(2000);
  }
  if (!adsReady) {
    adsReady = startADS();
    if (adsReady) Serial.println("ADS1115 terdeteksi.");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("pH + ADS1115 + Blynk");

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  Wire.setTimeOut(20);

  adsReady = startADS();
  if (!adsReady) Serial.println("ADS1115 tidak terdeteksi. Periksa I2C.");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Blynk.config(BLYNK_AUTH_TOKEN);

  timer.setInterval(SAMPLE_INTERVAL, takeSample);
  timer.setInterval(SEND_INTERVAL, sendPH);
  timer.setInterval(LINK_INTERVAL, keepLinks);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && Blynk.connected()) Blynk.run();
  timer.run();
}
