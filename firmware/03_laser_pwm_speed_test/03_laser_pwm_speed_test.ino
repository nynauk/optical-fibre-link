#include <Arduino.h>
#if __has_include(<esp_arduino_version.h>)
  #include <esp_arduino_version.h>
#endif

#define LASER_PIN 25
#define PWM_CHANNEL 0
#define PWM_RES_BITS 8

uint32_t currentFreq = 0;

void attachLaserPwm(uint32_t frequency) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(LASER_PIN, frequency, PWM_RES_BITS);
#else
  ledcSetup(PWM_CHANNEL, frequency, PWM_RES_BITS);
  ledcAttachPin(LASER_PIN, PWM_CHANNEL);
#endif
}

void writeLaserDuty(uint32_t duty) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(LASER_PIN, duty);
#else
  ledcWrite(PWM_CHANNEL, duty);
#endif
}

void setFrequency(uint32_t frequency) {
  currentFreq = frequency;
  if (frequency == 0) {
    writeLaserDuty(0);
    digitalWrite(LASER_PIN, LOW);
    Serial.println(F("Laser OFF"));
    return;
  }

  attachLaserPwm(frequency);
  writeLaserDuty(128);

  Serial.print(F("Laser PWM frequency set to: "));
  Serial.print(frequency);
  Serial.println(F(" Hz"));
}

void printMenu() {
  Serial.println();
  Serial.println(F("===== LASER PWM SPEED TEST ====="));
  Serial.println(F("1: 20 Hz"));
  Serial.println(F("2: 50 Hz"));
  Serial.println(F("3: 1 kHz"));
  Serial.println(F("4: 2 kHz"));
  Serial.println(F("5: 5 kHz"));
  Serial.println(F("6: 10 kHz"));
  Serial.println(F("7: 128 kHz"));
  Serial.println(F("8: 256 kHz"));
  Serial.println(F("9: 512 kHz"));
  Serial.println(F("0: OFF"));
}

void setup() {
  Serial.begin(115200);
  pinMode(LASER_PIN, OUTPUT);
  digitalWrite(LASER_PIN, LOW);
  delay(500);
  printMenu();
}

void loop() {
  if (!Serial.available()) return;

  char c = Serial.read();
  switch (c) {
    case '1': setFrequency(20); break;
    case '2': setFrequency(50); break;
    case '3': setFrequency(1000); break;
    case '4': setFrequency(2000); break;
    case '5': setFrequency(5000); break;
    case '6': setFrequency(10000); break;
    case '7': setFrequency(128000); break;
    case '8': setFrequency(256000); break;
    case '9': setFrequency(512000); break;
    case '0': setFrequency(0); break;
    case 'm':
    case 'M': printMenu(); break;
  }
}
