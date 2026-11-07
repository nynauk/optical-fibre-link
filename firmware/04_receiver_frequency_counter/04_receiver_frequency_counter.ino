#include <Arduino.h>

#define RX_DIGITAL_PIN 34

volatile uint32_t edgeCount = 0;
volatile uint32_t lastRiseUs = 0;
volatile uint32_t periodUs = 0;

void IRAM_ATTR onRisingEdge() {
  uint32_t now = micros();
  edgeCount++;
  if (lastRiseUs != 0) {
    periodUs = now - lastRiseUs;
  }
  lastRiseUs = now;
}

void setup() {
  Serial.begin(115200);
  pinMode(RX_DIGITAL_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(RX_DIGITAL_PIN), onRisingEdge, RISING);
  Serial.println(F("Comparator frequency counter started."));
}

void loop() {
  static uint32_t lastMs = 0;

  if (millis() - lastMs >= 1000) {
    lastMs += 1000;

    noInterrupts();
    uint32_t count = edgeCount;
    uint32_t pUs = periodUs;
    edgeCount = 0;
    interrupts();

    float countFrequency = count;
    float periodFrequency = (pUs > 0) ? (1000000.0f / pUs) : 0.0f;

    Serial.print(F("Count method: "));
    Serial.print(countFrequency, 2);
    Serial.print(F(" Hz | Period method: "));
    Serial.print(periodFrequency, 2);
    Serial.println(F(" Hz"));
  }
}
