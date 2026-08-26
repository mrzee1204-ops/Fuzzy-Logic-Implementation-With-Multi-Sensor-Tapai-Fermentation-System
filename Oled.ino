#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Ukuran OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Alamat I2C OLED
#define OLED_ADDR 0x3C

// Membuat objek display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void setup() {

  Serial.begin(115200);

  // Inisialisasi I2C
  Wire.begin(21, 22);

  // Inisialisasi OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {

    Serial.println("OLED gagal terhubung");
    while(1);
  }

  // Bersihkan layar
  display.clearDisplay();

  // Atur ukuran teks
  display.setTextSize(1);

  // Warna teks
  display.setTextColor(SSD1306_WHITE);

  // Posisi cursor
  display.setCursor(0,0);

  // Tampilkan tulisan
  display.println("ESP32 DEVKITC V4");
  display.println("OLED 0.96 INCH");
  display.println("128 x 64 SSD1306");

  display.println("----------------");

  display.println("Sistem Berjalan");

  // Tampilkan ke OLED
  display.display();
}

void loop() {

}