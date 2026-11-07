#include <Arduino.h>

#define RX_DIGITAL_PIN 34

int lastState = -1;

void setup() {
  Serial.begin(115200);
  pinMode(RX_DIGITAL_PIN, INPUT);
  Serial.println(F("Comparator digital monitor started."));
}

void loop() {
  int state = digitalRead(RX_DIGITAL_PIN);
  if (state != lastState) {
    lastState = state;
    Serial.print(F("Comparator output: "));
    Serial.println(state == HIGH ? F("HIGH") : F("LOW"));
  }
}
