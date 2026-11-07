#include <Arduino.h>

#define LASER_PIN      25
#define BPW34_ADC_PIN  34

static const uint32_t SERIAL_BAUD = 115200;
static const uint16_t THRESHOLD = 4000;
static const uint32_t BIT_US = 100000;

enum TxState { TX_IDLE, TX_START, TX_BITS, TX_STOP, TX_GAP };
enum RxState { RX_WAIT_IDLE, RX_WAIT_START, RX_BITS, RX_STOP };

TxState txState = TX_IDLE;
RxState rxState = RX_WAIT_IDLE;

uint8_t txByte = 0;
uint8_t rxByte = 0;
uint8_t txBit = 0;
uint8_t rxBit = 0;
uint32_t txNextUs = 0;
uint32_t rxNextUs = 0;

static const uint16_t TX_BUF_SIZE = 256;
static const uint16_t RX_BUF_SIZE = 256;
uint8_t txBuf[TX_BUF_SIZE];
uint8_t rxBuf[RX_BUF_SIZE];
volatile uint16_t txHead = 0, txTail = 0;
volatile uint16_t rxHead = 0, rxTail = 0;

uint16_t nextIndex(uint16_t i, uint16_t size) { return (i + 1) % size; }

bool txPush(uint8_t b) {
  uint16_t n = nextIndex(txHead, TX_BUF_SIZE);
  if (n == txTail) return false;
  txBuf[txHead] = b;
  txHead = n;
  return true;
}

bool txPop(uint8_t &b) {
  if (txHead == txTail) return false;
  b = txBuf[txTail];
  txTail = nextIndex(txTail, TX_BUF_SIZE);
  return true;
}

bool rxPush(uint8_t b) {
  uint16_t n = nextIndex(rxHead, RX_BUF_SIZE);
  if (n == rxTail) return false;
  rxBuf[rxHead] = b;
  rxHead = n;
  return true;
}

bool rxPop(uint8_t &b) {
  if (rxHead == rxTail) return false;
  b = rxBuf[rxTail];
  rxTail = nextIndex(rxTail, RX_BUF_SIZE);
  return true;
}

inline bool opticalBitHigh() {
  return analogRead(BPW34_ADC_PIN) >= THRESHOLD;
}

inline void laserWriteBit(bool bitValue) {
  digitalWrite(LASER_PIN, bitValue ? HIGH : LOW);
}

void drawMenu() {
  Serial.println();
  Serial.println(F("=============================================="));
  Serial.println(F(" SH2 OPTICAL FIBRE TRANSCEIVER - 10 Hz TEST"));
  Serial.println(F("=============================================="));
  Serial.println(F("TX section: type text and press Enter."));
  Serial.println(F("RX section: received text appears below."));
  Serial.print  (F("Fixed threshold: "));
  Serial.println(THRESHOLD);
  Serial.println(F("----------------------------------------------"));
}

void queueTextForTransmission(const String &msg) {
  Serial.print(F("TX: "));
  Serial.println(msg);

  uint8_t len = (uint8_t)min((int)msg.length(), 240);
  uint8_t crc = len;

  txPush(0x7E);
  txPush(len);
  for (uint8_t i = 0; i < len; i++) {
    uint8_t b = (uint8_t)msg[i];
    txPush(b);
    crc ^= b;
  }
  txPush(crc);
}

void txTask() {
  uint32_t now = micros();

  switch (txState) {
    case TX_IDLE:
      if (txPop(txByte)) {
        txBit = 0;
        txNextUs = now;
        txState = TX_START;
      }
      break;

    case TX_START:
      if ((int32_t)(now - txNextUs) >= 0) {
        laserWriteBit(true);
        txNextUs += BIT_US;
        txState = TX_BITS;
      }
      break;

    case TX_BITS:
      if ((int32_t)(now - txNextUs) >= 0) {
        laserWriteBit((txByte >> txBit) & 0x01);
        txBit++;
        txNextUs += BIT_US;
        if (txBit >= 8) txState = TX_STOP;
      }
      break;

    case TX_STOP:
      if ((int32_t)(now - txNextUs) >= 0) {
        laserWriteBit(false);
        txNextUs += BIT_US;
        txState = TX_GAP;
      }
      break;

    case TX_GAP:
      if ((int32_t)(now - txNextUs) >= 0) txState = TX_IDLE;
      break;
  }
}

void rxTask() {
  uint32_t now = micros();

  switch (rxState) {
    case RX_WAIT_IDLE:
      if (!opticalBitHigh()) rxState = RX_WAIT_START;
      break;

    case RX_WAIT_START:
      if (opticalBitHigh()) {
        rxByte = 0;
        rxBit = 0;
        rxNextUs = now + BIT_US + (BIT_US / 2);
        rxState = RX_BITS;
      }
      break;

    case RX_BITS:
      if ((int32_t)(now - rxNextUs) >= 0) {
        if (opticalBitHigh()) rxByte |= (1 << rxBit);
        rxBit++;
        rxNextUs += BIT_US;
        if (rxBit >= 8) rxState = RX_STOP;
      }
      break;

    case RX_STOP:
      if ((int32_t)(now - rxNextUs) >= 0) {
        rxPush(rxByte);
        rxState = RX_WAIT_IDLE;
      }
      break;
  }
}

uint8_t pState = 0;
uint8_t pLen = 0;
uint8_t pIdx = 0;
uint8_t pCrc = 0;
char payload[241];

void parseReceivedBytes() {
  uint8_t b;
  while (rxPop(b)) {
    switch (pState) {
      case 0:
        if (b == 0x7E) pState = 1;
        break;

      case 1:
        pLen = b;
        pIdx = 0;
        pCrc = b;
        if (pLen > 240) pState = 0;
        else pState = (pLen == 0) ? 3 : 2;
        break;

      case 2:
        payload[pIdx++] = (char)b;
        pCrc ^= b;
        if (pIdx >= pLen) pState = 3;
        break;

      case 3:
        payload[pLen] = '\0';
        if (b == pCrc) {
          Serial.print(F("RX: "));
          Serial.println(payload);
        } else {
          Serial.println(F("RX: packet rejected - checksum failed"));
        }
        pState = 0;
        break;
    }
  }
}

void readSerialInput() {
  static String input = "";

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (input.length() > 0) {
        queueTextForTransmission(input);
        input = "";
      }
    } else {
      input += c;
      if (input.length() >= 240) {
        queueTextForTransmission(input);
        input = "";
      }
    }
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  pinMode(LASER_PIN, OUTPUT);
  digitalWrite(LASER_PIN, LOW);
  analogReadResolution(12);
  delay(500);
  drawMenu();
}

void loop() {
  readSerialInput();
  txTask();
  rxTask();
  parseReceivedBytes();
}
