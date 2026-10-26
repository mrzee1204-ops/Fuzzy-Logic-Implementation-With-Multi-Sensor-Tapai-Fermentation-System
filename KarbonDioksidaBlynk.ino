#define BLYNK_TEMPLATE_ID   "TMPL67XsGK-JH"
#define BLYNK_TEMPLATE_NAME "Konsentrasi Karbon Dioksida"
#define BLYNK_AUTH_TOKEN    "NalAIWngjVcWJe9A9jNeJ7QJPg6poXAM"

#include <WiFi.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_ADS1X15.h>
#include <BlynkSimpleEsp32_SSL.h>

const char* ssid     = "Kos Pria Tampan";
const char* password = "kosanpriatampan03";

const int     SDA_PIN     = 21;
const int     SCL_PIN     = 22;
const uint8_t ADS_ADDRESS = 0x48;
const uint8_t MQ135_CHANNEL = 2;

const float MQ135_SCALE   = 1.0;
const float SENSOR_SUPPLY = 5.0;
const float RL = 2000.0;
const float Ro = 9000.0;
const float a = 110.47;
const float b = -2.86;
const float minPPM = 10.0;
const float maxPPM = 1000.0;
const int   AVG_SAMPLES = 8;

const unsigned long READ_INTERVAL = 1000;
const unsigned long LINK_INTERVAL = 5000;
const unsigned long MQ135_WARMUP  = 300000UL;

#define VPIN_CO2 V3

Adafruit_ADS1115 ads;
BlynkTimer timer;
bool adsReady = false;

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
float readMQ135Voltage() {
  if (!adsReady || !i2cAvailable()) { adsReady = false; return NAN; }
  float sum = 0;
  int count = 0;
  for (int i = 0; i < AVG_SAMPLES; i++) {
    int16_t raw = ads.readADC_SingleEnded(MQ135_CHANNEL);
    if (!i2cAvailable()) { adsReady = false; return NAN; }
    float v = ads.computeVolts(raw);
    if (v >= 0) { sum += v; count++; }
  }
  if (count < AVG_SAMPLES / 2) return NAN;
  float volts = (sum / count) * MQ135_SCALE;
  if (volts <= 0.01f || volts >= SENSOR_SUPPLY - 0.01f) return NAN;
  return volts;
}

void readAndSend() {
  float volts = readMQ135Voltage();
  if (isnan(volts)) {
    Serial.println("MQ-135 tidak valid. Cek ADS1115 dan kabel AO ke A2.");
    return;
  }

  // Rs = RL * (Vc / Vout - 1), lalu ppm = a * (Rs/Ro)^b
  float rs  = RL * (SENSOR_SUPPLY / volts - 1.0f);
  float ppm = a * powf(rs / Ro, b);
  bool warm = millis() >= MQ135_WARMUP;

  if (!isfinite(ppm) || ppm < minPPM || ppm > maxPPM) {
    Serial.printf("A2: %.3f V | Rs: %.0f Ohm | Diluar jangkauan (%s)\n",
                  volts, rs, warm ? "siap" : "pemanasan");
    return;
  }

  Serial.printf("A2: %.3f V | Rs: %.0f Ohm | CO2: %.1f ppm | %s | Blynk: %s\n",
                volts, rs, ppm, warm ? "siap" : "pemanasan",
                Blynk.connected() ? "terhubung" : "putus");

  if (Blynk.connected()) Blynk.virtualWrite(VPIN_CO2, ppm);
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
  Serial.println("MQ-135 + ADS1115 + Blynk");

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  Wire.setTimeOut(20);

  adsReady = startADS();
  if (!adsReady) Serial.println("ADS1115 tidak terdeteksi. Periksa I2C.");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Blynk.config(BLYNK_AUTH_TOKEN);

  timer.setInterval(READ_INTERVAL, readAndSend);
  timer.setInterval(LINK_INTERVAL, keepLinks);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && Blynk.connected()) Blynk.run();
  timer.run();
}
