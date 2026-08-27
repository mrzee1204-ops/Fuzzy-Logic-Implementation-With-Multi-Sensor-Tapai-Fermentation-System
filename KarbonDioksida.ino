//Use Analog Pin from Microcontroller
const int mqPin = 34; 
const float RL = 2000.0;
// RL is Load Resistance 

// Resistance value from clean air without contamination of another gases
const float Ro = 6000.00; 

// a and b value we get from Power Regression Calculator
const float a = 110.47; 
const float b = -2.86;  

// Batas baca sensor (dari Datasheet)
const float minPPM = 10.0;
const float maxPPM = 1000.0;

void setup() {
  // ESP32 biasanya menggunakan baud rate 115200
  Serial.begin(115200); 
  Serial.println("Memulai Deteksi CO2 MQ-135 di ESP32...");
  
  // Mengatur resolusi ADC ke 12-bit (0-4095)
  analogReadResolution(12);
}

void loop() {
  int adcRaw = analogRead(mqPin);
  
  // Mencegah error pembagian dengan nol jika adcRaw terbaca 0
  if(adcRaw == 0) adcRaw = 1;
  
  // Kalkulasi nilai Rs dan rasio Rs/Ro
  // PENTING: Resolusi ADC ESP32 adalah 4095 (bukan 1023 seperti Arduino)
  float Rs = RL * ((4095.0 / (float)adcRaw) - 1.0);
  float ratio = Rs / Ro;
  
  // Rumus konversi Rasio ke PPM: PPM = a * (Rs/Ro)^b
  float ppm = a * pow(ratio, b);
  
  // Logika pembatasan pembacaan
  if(ppm < minPPM || ppm > maxPPM) {
    Serial.println("Diluar jangkauan");
  } else {
    Serial.print("CO2: ");
    Serial.print(ppm);
    Serial.println(" PPM");
  }
  
  delay(1000); // Jeda 1 detik
}
