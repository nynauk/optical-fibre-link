#include <Arduino.h>

#define BPW34_ADC_PIN 34
#define THRESHOLD     4000

uint16_t minSeen = 4095;
uint16_t maxSeen = 0;

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  delay(500);
  Serial.println(F("BPW34 analogue monitor started."));
  Serial.println(F("RAW, MIN, MAX, STATE"));
}

void loop() {
  uint16_t raw = analogRead(BPW34_ADC_PIN);
  if (raw < minSeen) minSeen = raw;
  if (raw > maxSeen) maxSeen = raw;

  Serial.print(raw);
  Serial.print(F(", "));
  Serial.print(minSeen);
  Serial.print(F(", "));
  Serial.print(maxSeen);
  Serial.print(F(", "));
  Serial.println(raw >= THRESHOLD ? F("LASER_ON") : F("LASER_OFF"));

  delay(100);
}
