int adcPin = 34;
float voltage, pH;

float m = -6.418;
float b = 17.226;
float correction = 0.00;

const int numSamples = 20;
int samples[numSamples];
int indexSample = 0;
long total = 0;
int filledCount = 0;
float average = 0;
float volt = 3.3;

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < numSamples; i++) {
    samples[i] = 0;
  }
}
void loop() {
  total = total - samples[indexSample];
  samples[indexSample] = analogRead(adcPin);
  total = total + samples[indexSample];
  indexSample = (indexSample + 1) % numSamples;

  if (filledCount < numSamples) filledCount++;  
  average = (float)total / filledCount;

  voltage = average * (volt / 4095.0);

  pH = (m * voltage + b) + correction;
  Serial.print("Voltage: ");
  Serial.print(voltage, 3);
  Serial.print("  pH: ");
  Serial.println(pH, 2);

  delay(100);
}
