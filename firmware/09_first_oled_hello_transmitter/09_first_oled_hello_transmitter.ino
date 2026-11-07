#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// ---------- OLED SETUP ----------
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

const int SCREEN_W = 128;
const int SCREEN_H = 64;

// ---------- PINS (ESP32 TX) ----------
const int LASER_PIN  = 25;   // GPIO25 -> laser / LED driver via transistor
const int BUTTON_PIN = 26;   // shared button line to GND on both boards

// ---------- TX CONFIG ----------
const unsigned long BIT_DURATION_MS = 250;   // 0.25 s per bit (must match RX)
const char *MESSAGE = "HELLO";

// ---------- BUTTON STATE ----------
int lastButtonState = HIGH;  // INPUT_PULLUP: idle = HIGH, pressed = LOW

// ---------- TX BIT/BYTE LOGIC ----------

// Send a single bit: 1 = laser ON, 0 = laser OFF
void sendBit(uint8_t bit) {
  digitalWrite(LASER_PIN, bit ? HIGH : LOW);
  delay(BIT_DURATION_MS);
}

// Send 4 '1' bits as preamble (1111)
void sendPreamble() {
  for (int i = 0; i < 4; i++) {
    sendBit(1);
  }
}

// Send one byte MSB-first (bit 7 → bit 0)
void sendByte(uint8_t b) {
  for (int i = 7; i >= 0; i--) {
    uint8_t bit = (b >> i) & 0x01;
    sendBit(bit);
  }
}

// Send the whole message once: preamble + MESSAGE
void sendMessageOnce() {
  // Status on OLED
  display.clearBuffer();
  display.setFont(u8g2_font_5x8_mf);
  display.drawStr(10, 24, "TX: SENDING HELLO");
  display.sendBuffer();

  sendPreamble();

  const char *p = MESSAGE;
  while (*p) {
    sendByte((uint8_t)*p);
    p++;
  }

  // Ensure laser is OFF at the end
  digitalWrite(LASER_PIN, LOW);
}

// ---------- OLED IDLE SCREEN ----------
int sweepX = 0;

void drawIdleScreen() {
  display.clearBuffer();

  int barY = 16;
  int barH = 10;

  display.drawFrame(0, barY, SCREEN_W, barH);
  display.drawBox(sweepX, barY + 2, 10, barH - 4);

  sweepX += 2;
  if (sweepX > SCREEN_W) sweepX = -10;

  display.setFont(u8g2_font_5x8_mf);
  display.drawStr(2, SCREEN_H - 10, "TX: HELLO @ 250ms");
  display.drawStr(2, SCREEN_H - 2,  "Press BTN (D26)");

  display.sendBuffer();
}

// ---------- SETUP & LOOP ----------
void setup() {
  // Pins
  pinMode(LASER_PIN, OUTPUT);
  digitalWrite(LASER_PIN, LOW);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // I2C + OLED
  Wire.begin(21, 22);          // SDA=21, SCL=22
  display.begin();

  display.clearBuffer();
  display.setFont(u8g2_font_5x8_mf);
  display.drawStr(10, 24, "ESP32 TX READY");
  display.sendBuffer();
}

void loop() {
  int buttonState = digitalRead(BUTTON_PIN);

  // Edge: HIGH -> LOW = button press
  if (buttonState == LOW && lastButtonState == HIGH) {
    sendMessageOnce();
  }

  lastButtonState = buttonState;

  drawIdleScreen();
  delay(40);   // just for animation pacing
}
