#include <DHT.h>

#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

float suhu;
float kelembaban;

void setup() {

  Serial.begin(115200);
  Serial.println("======================================");
  Serial.println(" KARAKTERISASI SENSOR DHT22 ESP32");
  Serial.println("======================================");

  dht.begin();
}
void loop() {
  suhu = dht.readTemperature();
  kelembaban = dht.readHumidity();

  if (isnan(suhu) || isnan(kelembaban)) {
    Serial.println("Gagal membaca sensor DHT22");
    delay(2000);
    return;
  }

  // Tampilkan data
  Serial.print("Suhu       : ");
  Serial.print(suhu, 2);
  Serial.print(" °C");

  Serial.print(" | Kelembaban : ");
  Serial.print(kelembaban, 2);
  Serial.println(" %");

  delay(1000);
}
