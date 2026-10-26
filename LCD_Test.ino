#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <string.h>

const int SDA_PIN = 21;
const int SCL_PIN = 22;
const uint8_t LCD_ADDRESS_1 = 0x27;
const int LCD_COLS = 16;
const int LCD_ROWS = 2;

const char* BARIS_1 = "Fahro Rozi";
const char* BARIS_2 = "2210441009";
const bool  RATA_TENGAH = false;

LiquidCrystal_I2C* lcd = nullptr;

bool i2cAvailable(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void tulisBaris(int row, const char* text) {
  int len = strlen(text);
  if (len > LCD_COLS) len = LCD_COLS;
  int col = RATA_TENGAH ? (LCD_COLS - len) / 2 : 0;
  lcd->setCursor(col, row);
  for (int i = 0; i < len; i++) lcd->write(text[i]);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  Wire.setTimeOut(20);

  uint8_t address = 0;
  if (i2cAvailable(LCD_ADDRESS_1)) address = LCD_ADDRESS_1;
  else if (i2cAvailable(LCD_ADDRESS_2)) address = LCD_ADDRESS_2;

  if (address == 0) {
    Serial.println("LCD tidak terdeteksi di 0x27 maupun 0x3F. Periksa SDA/SCL, VCC 5 V, dan GND.");
    return;
  }
  Serial.printf("LCD terdeteksi di alamat 0x%02X\n", address);

  lcd = new LiquidCrystal_I2C(address, LCD_COLS, LCD_ROWS);
  lcd->init();
  lcd->backlight();
  lcd->clear();
  tulisBaris(0, BARIS_1);
  tulisBaris(1, BARIS_2);
  Serial.println("Teks sudah ditampilkan.");
}

void loop() {
}
