#include <Arduino.h>

#define LASER_PIN 25

void setup() {
  pinMode(LASER_PIN, OUTPUT);
}

void loop() {
  digitalWrite(LASER_PIN, HIGH);
  delay(1000);
  digitalWrite(LASER_PIN, LOW);
  delay(1000);
}
