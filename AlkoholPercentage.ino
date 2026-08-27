// Gunakan pin ADC1 pada ESP32 (misalnya GPIO 34)
#define MQ3_PIN 34

float vinMQ3 = 0.0;
float alkohol = 0.0;

void setup() {
  // ESP32 biasanya menggunakan baud rate 115200
  Serial.begin(115200);
  
  // Mengatur resolusi ADC ke 12-bit (0 - 4095)
  analogReadResolution(12);
  
  Serial.println("Memulai Deteksi Alkohol dengan Sensor MQ-3...");
  delay(2000);
}

void loop() {
  // 1. Membaca nilai analog (ADC) dari sensor MQ-3
  int mq3ADC = analogRead(MQ3_PIN);
  
  // 2. Mengonversi nilai ADC menjadi tegangan keluaran ESP32 (3.3V max)
  float voutMQ3 = mq3ADC * 3.3 / 4095.0;

  // 3. Menghitung tegangan asli (Vin) sebelum masuk Voltage Divider
  // Asumsi: R1 (atas) = 1000 Ohm (1k), R2 (bawah ke ground) = 2000 Ohm (2k)
  float R1 = 1000.0;
  float R2 = 2000.0;
  vinMQ3 = voutMQ3 * (R1 + R2) / R2;

  // 4. Menggunakan rumus regresi linear untuk konversi ke % v/v
  // Angka ini (20.036 dan 0.08) perlu disesuaikan dengan kalibrasi sensor Anda
  alkohol = a * vinMQ3 + b;
  // a = slope ; b = intercept from linear regression y = ax+b

  // 5. Menampilkan hasil ke Serial Monitor
  Serial.print("MQ3 ADC: ");
  Serial.print(mq3ADC);
  Serial.print(" | Vin Asli: ");
  Serial.print(vinMQ3, 3); // Cetak 3 angka di belakang koma
  Serial.print(" V | Kadar Alkohol: ");
  Serial.print(alkohol, 2); // Cetak 2 angka di belakang koma
  Serial.println(" %");

  // Jeda 2 detik sebelum pembacaan berikutnya
  delay(2000); 
}
