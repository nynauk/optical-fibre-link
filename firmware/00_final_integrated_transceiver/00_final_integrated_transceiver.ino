#include <SPI.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>

#include "AudioTools.h"
#include "BluetoothA2DPSink.h"

#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4
#define TFT_SCK  18
#define TFT_MISO 19
#define TFT_MOSI 22

#define TOUCH_CS   21
#define TOUCH_IRQ  26

#define TFT_BL_PIN 32

#define LASER_PIN 25
#define BPW34_IN  34

#define I2S_BCLK 27
#define I2S_LRC  14
#define I2S_DOUT 23

static const uint32_t BIT_US = 750;
static const int THRESH_FIXED = 1500;

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
Preferences prefs;

I2SStream i2s;
BluetoothA2DPSink a2dp_sink(i2s);
bool bluetoothActive = false;

enum SystemMode { MODE_MENU, MODE_DATA, MODE_LIVE, MODE_BT, MODE_CREDITS };
SystemMode currentMode = MODE_MENU;
SystemMode lastMode    = MODE_MENU;

bool screenNeedsRedraw = true;

enum TxState { TX_IDLE, TX_START, TX_DATA, TX_STOP, TX_GAP };
TxState txState = TX_IDLE;

uint8_t txByte = 0;
int txBitIndex = 0;
uint32_t txNextUs = 0;

enum RxState { RX_WAIT_IDLE, RX_WAIT_START, RX_DATA, RX_STOP };
RxState rxState = RX_WAIT_IDLE;

uint8_t rxByte = 0;
int rxBitIndex = 0;
uint32_t rxNextUs = 0;

struct Rect { int x,y,w,h; };
struct TouchPoint { int x; int y; bool pressed; };

struct TouchCal {
  int minX, maxX, minY, maxY;
  bool swapXY;
  bool invX;
  bool invY;
  bool valid;
};

TouchCal cal = { 300, 3800, 300, 3800, false, false, false, false };

static inline int iabs(int v) { return v < 0 ? -v : v; }

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
  return (x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh));
}

int mapClamp(int v, int inMin, int inMax, int outMin, int outMax) {
  if (inMin == inMax) return outMin;
  if (inMin > inMax) { int t=inMin; inMin=inMax; inMax=t; }
  if (v < inMin) v = inMin;
  if (v > inMax) v = inMax;
  long num = (long)(v - inMin) * (outMax - outMin);
  long den = (inMax - inMin);
  return outMin + (int)(num / den);
}

bool loadCal() {
  prefs.begin("touch", true);
  bool ok = prefs.getBool("valid", false);
  if (!ok) { prefs.end(); return false; }

  cal.minX   = prefs.getInt("minX", 300);
  cal.maxX   = prefs.getInt("maxX", 3800);
  cal.minY   = prefs.getInt("minY", 300);
  cal.maxY   = prefs.getInt("maxY", 3800);
  cal.swapXY = prefs.getBool("swap", false);
  cal.invX   = prefs.getBool("invX", false);
  cal.invY   = prefs.getBool("invY", false);
  cal.valid  = true;

  prefs.end();
  return true;
}

void saveCal() {
  prefs.begin("touch", false);
  prefs.putBool("valid", true);
  prefs.putInt("minX", cal.minX);
  prefs.putInt("maxX", cal.maxX);
  prefs.putInt("minY", cal.minY);
  prefs.putInt("maxY", cal.maxY);
  prefs.putBool("swap", cal.swapXY);
  prefs.putBool("invX", cal.invX);
  prefs.putBool("invY", cal.invY);
  prefs.end();
}

void clearCal() {
  prefs.begin("touch", false);
  prefs.clear();
  prefs.end();
  cal.valid = false;
}

TS_Point readRawTouchStable() {
  while (!ts.touched()) delay(1);

  const int N = 6;
  long sx=0, sy=0, sz=0;
  for (int i=0;i<N;i++) {
    TS_Point p = ts.getPoint();
    sx += p.x; sy += p.y; sz += p.z;
    delay(8);
  }
  TS_Point out((int)(sx/N), (int)(sy/N), (int)(sz/N));

  while (ts.touched()) delay(1);
  delay(120);
  return out;
}

void drawTarget(int x, int y, uint16_t color) {
  tft.drawLine(x-12, y, x+12, y, color);
  tft.drawLine(x, y-12, x, y+12, color);
  tft.drawCircle(x, y, 14, color);
}

void calibrateTouch() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Touch Cal");
  tft.setTextSize(1);
  tft.setCursor(10, 40);
  tft.print("Tap targets with stylus.");

  const int W = tft.width();
  const int H = tft.height();
  const int M = 20;

  struct Raw { int x; int y; };
  Raw TLp, TRp, BRp, BLp;

  tft.fillRect(0, 60, W, 20, ILI9341_BLACK);
  tft.setCursor(10, 60); tft.print("Tap TOP-LEFT");
  drawTarget(M, M+40, ILI9341_GREEN);
  TS_Point p1 = readRawTouchStable(); TLp.x = p1.x; TLp.y = p1.y;

  tft.fillRect(0, 60, W, 20, ILI9341_BLACK);
  tft.setCursor(10, 60); tft.print("Tap TOP-RIGHT");
  drawTarget(W-M, M+40, ILI9341_GREEN);
  TS_Point p2 = readRawTouchStable(); TRp.x = p2.x; TRp.y = p2.y;

  tft.fillRect(0, 60, W, 20, ILI9341_BLACK);
  tft.setCursor(10, 60); tft.print("Tap BOTTOM-RIGHT");
  drawTarget(W-M, H-M, ILI9341_GREEN);
  TS_Point p3 = readRawTouchStable(); BRp.x = p3.x; BRp.y = p3.y;

  tft.fillRect(0, 60, W, 20, ILI9341_BLACK);
  tft.setCursor(10, 60); tft.print("Tap BOTTOM-LEFT");
  drawTarget(M, H-M, ILI9341_GREEN);
  TS_Point p4 = readRawTouchStable(); BLp.x = p4.x; BLp.y = p4.y;

  int d_h_x = iabs(TRp.x - TLp.x);
  int d_h_y = iabs(TRp.y - TLp.y);
  cal.swapXY = (d_h_y > d_h_x);

  auto ax = [&](Raw r){ return cal.swapXY ? r.y : r.x; };
  auto ay = [&](Raw r){ return cal.swapXY ? r.x : r.y; };

  int tlx = ax(TLp), trx = ax(TRp), brx = ax(BRp), blx = ax(BLp);
  int tly = ay(TLp), try_ = ay(TRp), bry = ay(BRp), bly = ay(BLp);

  cal.minX = (tlx + blx) / 2;
  cal.maxX = (trx + brx) / 2;
  cal.minY = (tly + try_) / 2;
  cal.maxY = (bly + bry) / 2;

  cal.invX = (cal.maxX < cal.minX);
  if (cal.invX) { int t=cal.minX; cal.minX=cal.maxX; cal.maxX=t; }

  cal.invY = (cal.maxY < cal.minY);
  if (cal.invY) { int t=cal.minY; cal.minY=cal.maxY; cal.maxY=t; }

  cal.valid = true;
  saveCal();

  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(10, 60);
  tft.print("Saved!");
  delay(700);
}

TouchPoint readTouchMapped() {
  TouchPoint tp{0,0,false};
  if (!ts.touched() || !cal.valid) return tp;

  TS_Point p = ts.getPoint();
  int a = p.x, b = p.y;

  int rx = cal.swapXY ? b : a;
  int ry = cal.swapXY ? a : b;

  int x = mapClamp(rx, cal.minX, cal.maxX, 0, tft.width()  - 1);
  int y = mapClamp(ry, cal.minY, cal.maxY, 0, tft.height() - 1);

  if (cal.invX) x = (tft.width()  - 1) - x;
  if (cal.invY) y = (tft.height() - 1) - y;

  tp.x = x; tp.y = y; tp.pressed = true;
  return tp;
}

bool touchPressEvent(int &outX, int &outY) {
  static bool wasDown = false;
  static uint32_t lastFire = 0;

  TouchPoint tp = readTouchMapped();

  if (tp.pressed && !wasDown) {
    wasDown = true;
    if (millis() - lastFire < 180) return false;
    lastFire = millis();
    outX = tp.x; outY = tp.y;
    return true;
  }
  if (!tp.pressed) wasDown = false;
  return false;
}

Rect grid[4];
Rect backRect;

void calcGridLayout() {
  int W = tft.width();
  int H = tft.height();

  int pad = 10;
  int gutter = 10;
  int tileW = (W - 2*pad - gutter) / 2;
  int tileH = (H - 2*pad - gutter) / 2;

  grid[0] = { pad,                  pad,                  tileW, tileH };
  grid[1] = { pad + tileW + gutter, pad,                  tileW, tileH };
  grid[2] = { pad,                  pad + tileH + gutter, tileW, tileH };
  grid[3] = { pad + tileW + gutter, pad + tileH + gutter, tileW, tileH };

  backRect = { 0, 0, 150, 110 };
}

uint16_t tileFill(int i) {
  return (i % 2 == 0) ? ILI9341_NAVY : ILI9341_DARKCYAN;
}

void drawTile(const Rect& r, const char* label, const char* sub, uint16_t fill) {
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 20, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 20, ILI9341_WHITE);

  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(3);
  tft.setCursor(r.x + 16, r.y + 20);
  tft.print(label);

  tft.setTextSize(1);
  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setCursor(r.x + 16, r.y + r.h - 22);
  tft.print(sub);

  tft.setTextSize(2);
}

void drawMenuGrid() {
  tft.fillScreen(ILI9341_BLACK);
  drawTile(grid[0], "DATA", "Binary <-> Laser", tileFill(0));
  drawTile(grid[1], "LIVE", "BPW34 values",     tileFill(1));
  drawTile(grid[2], "BT",   "A2DP audio",       tileFill(2));
  drawTile(grid[3], "INFO", "Credits",          tileFill(3));
}

void drawBackOverlay() {
  tft.fillRoundRect(12, 12, 90, 52, 18, ILI9341_BLACK);
  tft.drawRoundRect(12, 12, 90, 52, 18, ILI9341_WHITE);
  tft.setTextSize(3);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(44, 26);
  tft.print("<");
  tft.setTextSize(2);
}

bool backButtonHit(int x, int y) {
  return hit(x, y, backRect.x, backRect.y, backRect.w, backRect.h);
}

void drawDataScreen() {
  tft.fillScreen(ILI9341_BLACK);
  drawBackOverlay();

  Rect box = { 12, 76, tft.width()-24, tft.height()-88 };
  tft.fillRoundRect(box.x, box.y, box.w, box.h, 18, ILI9341_NAVY);
  tft.drawRoundRect(box.x, box.y, box.w, box.h, 18, ILI9341_WHITE);

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(box.x + 16, box.y + 14);
  tft.print("RECEIVED (BINARY)");

  tft.setTextSize(1);
  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setCursor(box.x + 16, box.y + 42);
  tft.print("Send bytes. Shows packets. Tap < to back.");

  tft.fillRect(box.x + 16, box.y + 58, box.w - 32, box.h - 74, ILI9341_NAVY);
  tft.setTextSize(2);
}

void drawLiveScreen() {
  tft.fillScreen(ILI9341_BLACK);
  drawBackOverlay();

  Rect card = { 12, 76, tft.width()-24, tft.height()-88 };
  tft.fillRoundRect(card.x, card.y, card.w, card.h, 18, ILI9341_DARKCYAN);
  tft.drawRoundRect(card.x, card.y, card.w, card.h, 18, ILI9341_WHITE);

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(card.x + 16, card.y + 14);
  tft.print("BPW34 RAW");

  tft.setTextSize(1);
  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setCursor(card.x + 16, card.y + 42);
  tft.print("Tap < to go back");

  tft.fillRect(card.x + 16, card.y + 70, card.w - 32, 100, ILI9341_DARKCYAN);
  tft.setTextSize(2);
}

void drawBTScreen() {
  tft.fillScreen(ILI9341_BLACK);
  drawBackOverlay();

  Rect card = { 12, 76, tft.width()-24, tft.height()-88 };
  tft.fillRoundRect(card.x, card.y, card.w, card.h, 18, ILI9341_NAVY);
  tft.drawRoundRect(card.x, card.y, card.w, card.h, 18, ILI9341_WHITE);

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(card.x + 16, card.y + 14);
  tft.print("BLUETOOTH AUDIO");

  tft.setTextSize(1);
  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setCursor(card.x + 16, card.y + 42);
  tft.print("Pair: NYNA_AUDIO | Tap < to exit");

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(card.x + 16, card.y + 92);
  tft.print("1) Connect phone");
  tft.setCursor(card.x + 16, card.y + 124);
  tft.print("2) Play audio");
}

void drawCredits() {
  tft.fillScreen(ILI9341_BLACK);
  drawBackOverlay();

  Rect card = { 12, 76, tft.width()-24, tft.height()-88 };
  tft.fillRoundRect(card.x, card.y, card.w, card.h, 18, ILI9341_DARKCYAN);
  tft.drawRoundRect(card.x, card.y, card.w, card.h, 18, ILI9341_WHITE);

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(card.x + 16, card.y + 14);
  tft.print("NYNA TRANSCEIVER");

  tft.setTextSize(1);
  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setCursor(card.x + 16, card.y + 42);
  tft.print("Tap < to go back");

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(card.x + 16, card.y + 104);
  tft.print("Created by");
  tft.setCursor(card.x + 16, card.y + 136);
  tft.print("Ayaan Malik");
}

static const int TX_BUF_SIZE = 1024;
static const int RX_BUF_SIZE = 1024;

uint8_t txBuf[TX_BUF_SIZE];
volatile int txHead = 0, txTail = 0;

uint8_t rxBuf[RX_BUF_SIZE];
volatile int rxHead = 0, rxTail = 0;

static inline int txNext(int i){ return (i + 1) % TX_BUF_SIZE; }
static inline int rxNext(int i){ return (i + 1) % RX_BUF_SIZE; }

static inline bool txEmpty() { return txHead == txTail; }
static inline bool rxEmpty() { return rxHead == rxTail; }

bool txPush(uint8_t b) {
  int n = txNext(txHead);
  if (n == txTail) return false;
  txBuf[txHead] = b;
  txHead = n;
  return true;
}

bool txPop(uint8_t &b) {
  if (txEmpty()) return false;
  b = txBuf[txTail];
  txTail = txNext(txTail);
  return true;
}

bool rxPush(uint8_t b) {
  int n = rxNext(rxHead);
  if (n == rxTail) return false;
  rxBuf[rxHead] = b;
  rxHead = n;
  return true;
}

bool rxPop(uint8_t &b) {
  if (rxEmpty()) return false;
  b = rxBuf[rxTail];
  rxTail = rxNext(rxTail);
  return true;
}

bool laserSendPacket(const uint8_t *payload, uint8_t len) {
  if (!txPush(0x7E)) return false;
  if (!txPush(len))  return false;

  uint8_t crc = len;
  for (uint8_t i=0;i<len;i++) {
    if (!txPush(payload[i])) return false;
    crc ^= payload[i];
  }
  if (!txPush(crc)) return false;
  return true;
}

enum PState { P_WAIT_SYNC, P_WAIT_LEN, P_READ_PAYLOAD, P_WAIT_CRC };
PState pstate = P_WAIT_SYNC;
uint8_t plen = 0;
uint8_t pcrc = 0;
uint8_t ppay[256];
uint16_t pidx = 0;

bool packetReady = false;
uint8_t lastPacket[256];
uint8_t lastLen = 0;

void parseRxBytes() {
  uint8_t b;
  while (rxPop(b)) {
    switch (pstate) {
      case P_WAIT_SYNC:
        if (b == 0x7E) pstate = P_WAIT_LEN;
        break;

      case P_WAIT_LEN:
        plen = b;
        pcrc = b;
        pidx = 0;
        pstate = (plen == 0) ? P_WAIT_CRC : P_READ_PAYLOAD;
        break;

      case P_READ_PAYLOAD:
        ppay[pidx++] = b;
        pcrc ^= b;
        if (pidx >= plen) pstate = P_WAIT_CRC;
        break;

      case P_WAIT_CRC:
        if (b == pcrc) {
          lastLen = plen;
          for (uint16_t i=0;i<plen;i++) lastPacket[i] = ppay[i];
          packetReady = true;
        }
        pstate = P_WAIT_SYNC;
        break;
    }
  }
}

void sendTestBinary() {
  uint8_t p[] = { 0x00, 0x01, 0x7E, 0x10, 0x20, 0x41, 0x42, 0xFF };
  laserSendPacket(p, (uint8_t)sizeof(p));
}

inline bool readBit() { return analogRead(BPW34_IN) > THRESH_FIXED; }
inline void laserWrite(bool b) { digitalWrite(LASER_PIN, b ? HIGH : LOW); }

void txTask() {
  uint32_t now = micros();

  switch (txState) {
    case TX_IDLE: {
      uint8_t b;
      if (txPop(b)) {
        txByte = b;
        txState = TX_START;
        txNextUs = now;
      }
    } break;

    case TX_START:
      if ((int32_t)(now - txNextUs) >= 0) {

        laserWrite(true);
        txNextUs += BIT_US;
        txBitIndex = 0;
        txState = TX_DATA;
      }
      break;

    case TX_DATA:
      if ((int32_t)(now - txNextUs) >= 0) {
        laserWrite((txByte >> txBitIndex) & 1);
        txBitIndex++;
        txNextUs += BIT_US;
        if (txBitIndex >= 8) txState = TX_STOP;
      }
      break;

    case TX_STOP:
      if ((int32_t)(now - txNextUs) >= 0) {

        laserWrite(false);
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
      if (!readBit()) rxState = RX_WAIT_START;
      break;

    case RX_WAIT_START:
      if (readBit()) {
        rxNextUs = now + BIT_US + BIT_US / 2;
        rxBitIndex = 0;
        rxByte = 0;
        rxState = RX_DATA;
      }
      break;

    case RX_DATA:
      if ((int32_t)(now - rxNextUs) >= 0) {
        if (readBit()) rxByte |= (1 << rxBitIndex);
        rxBitIndex++;
        rxNextUs += BIT_US;
        if (rxBitIndex >= 8) rxState = RX_STOP;
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

void startBluetooth() {
  drawBTScreen();

  auto cfg = i2s.defaultConfig(TX_MODE);
  cfg.sample_rate = 44100;
  cfg.bits_per_sample = 16;
  cfg.channels = 2;
  cfg.buffer_size = 1024;
  cfg.buffer_count = 8;

  cfg.pin_bck  = I2S_BCLK;
  cfg.pin_ws   = I2S_LRC;
  cfg.pin_data = I2S_DOUT;

  i2s.begin(cfg);
  a2dp_sink.set_auto_reconnect(false);
  a2dp_sink.start("NYNA_AUDIO");

  bluetoothActive = true;
  delay(50);
}

void stopBluetooth() {
  if (!bluetoothActive) return;
  a2dp_sink.end();
  delay(50);
  i2s.end();
  bluetoothActive = false;
  delay(50);
}

void setBacklight100() {
  ledcSetup(0, 20000, 8);
  ledcAttachPin(TFT_BL_PIN, 0);
  ledcWrite(0, 255);
}

void setup() {
  Serial.begin(115200);

  pinMode(TFT_CS, OUTPUT);
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TOUCH_CS, HIGH);

  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);

  tft.begin();
  ts.begin();

  tft.setRotation(2);
  ts.setRotation(2);

  pinMode(TFT_BL_PIN, OUTPUT);
  setBacklight100();

  pinMode(LASER_PIN, OUTPUT);
  laserWrite(false);

  calcGridLayout();

  bool ok = loadCal();

  delay(250);
  if (ts.touched()) {
    clearCal();
    ok = false;
    while (ts.touched()) delay(1);
  }
  if (!ok) calibrateTouch();

  drawMenuGrid();
  screenNeedsRedraw = false;
}

void loop() {
  int tx=0, ty=0;
  bool tap = false;

  if (currentMode != MODE_BT) tap = touchPressEvent(tx, ty);

  if (currentMode != lastMode) {
    screenNeedsRedraw = true;
    lastMode = currentMode;
  }

  if (currentMode == MODE_MENU) {
    if (bluetoothActive) stopBluetooth();

    if (screenNeedsRedraw) {
      drawMenuGrid();
      screenNeedsRedraw = false;
    }

    if (tap) {
      for (int i=0;i<4;i++) {
        Rect r = grid[i];
        int ex=10, ey=10;
        if (hit(tx, ty, r.x-ex, r.y-ey, r.w+2*ex, r.h+2*ey)) {
          currentMode = (i==0) ? MODE_DATA :
                        (i==1) ? MODE_LIVE :
                        (i==2) ? MODE_BT :
                                 MODE_CREDITS;
          break;
        }
      }
    }
  }

  else if (currentMode == MODE_DATA) {
    if (bluetoothActive) stopBluetooth();

    if (screenNeedsRedraw) {
      drawDataScreen();
      packetReady = false;
      pstate = P_WAIT_SYNC;
      screenNeedsRedraw = false;
    }

    if (Serial.available() && Serial.peek() == 'T') {
      Serial.read();
      sendTestBinary();
    }

    static uint8_t chunk[64];
    static int cidx = 0;
    static uint32_t lastSerialMs = 0;

    while (Serial.available()) {
      int v = Serial.read();
      if (v < 0) break;
      chunk[cidx++] = (uint8_t)v;
      lastSerialMs = millis();

      if (cidx >= (int)sizeof(chunk)) {
        laserSendPacket(chunk, (uint8_t)cidx);
        cidx = 0;
      }
    }

    if (cidx > 0 && (millis() - lastSerialMs) > 15) {
      laserSendPacket(chunk, (uint8_t)cidx);
      cidx = 0;
    }

    txTask();
    rxTask();

    parseRxBytes();

    static uint32_t lastDraw = 0;
    if (packetReady && (millis() - lastDraw > 40)) {
      lastDraw = millis();
      packetReady = false;

      int boxX = 12, boxY = 76, boxW = tft.width()-24, boxH = tft.height()-88;
      int textX = boxX + 16;
      int textY = boxY + 58;
      int textW = boxW - 32;
      int textH = boxH - 74;

      tft.fillRect(textX, textY, textW, textH, ILI9341_NAVY);
      tft.setTextColor(ILI9341_WHITE);

      tft.setTextSize(2);
      tft.setCursor(textX, textY);
      tft.print("LEN=");
      tft.print(lastLen);

      tft.setTextSize(1);
      int y = textY + 22;
      int shown = (lastLen > 96) ? 96 : lastLen;

      for (int i=0;i<shown;i+=16) {
        tft.setCursor(textX, y);
        for (int j=0;j<16 && (i+j)<shown;j++) {
          uint8_t vv = lastPacket[i+j];
          if (vv < 16) tft.print('0');
          tft.print(vv, HEX);
          tft.print(' ');
        }
        y += 10;
        if (y > (textY + textH - 20)) break;
      }

      tft.setTextSize(1);
      tft.setCursor(textX, textY + textH - 12);
      for (int i=0;i<32 && i<lastLen;i++) {
        char c = (char)lastPacket[i];
        tft.print((c >= 32 && c <= 126) ? c : '.');
      }
    }

    if (tap && backButtonHit(tx, ty)) currentMode = MODE_MENU;
  }

  else if (currentMode == MODE_LIVE) {
    if (bluetoothActive) stopBluetooth();

    if (screenNeedsRedraw) {
      drawLiveScreen();
      screenNeedsRedraw = false;
    }

    static uint32_t lastUpdate = 0;
    if (millis() - lastUpdate > 140) {
      lastUpdate = millis();
      int raw = analogRead(BPW34_IN);

      int cardX=12, cardY=76, cardW=tft.width()-24;
      tft.fillRect(cardX + 16, cardY + 70, cardW - 32, 100, ILI9341_DARKCYAN);
      tft.setTextSize(5);
      tft.setTextColor(ILI9341_WHITE);
      tft.setCursor(cardX + 16, cardY + 96);
      tft.print(raw);
      tft.setTextSize(2);
    }

    if (tap && backButtonHit(tx, ty)) currentMode = MODE_MENU;
  }

  else if (currentMode == MODE_BT) {
    if (!bluetoothActive) {
      startBluetooth();
      screenNeedsRedraw = false;
    }

    static uint32_t lastTouchCheck = 0;
    if (millis() - lastTouchCheck > 50) {
      lastTouchCheck = millis();
      int x=0,y=0;
      bool t = touchPressEvent(x,y);
      if (t && backButtonHit(x,y)) {
        stopBluetooth();
        currentMode = MODE_MENU;
      }
    }

    delay(2);
  }

  else if (currentMode == MODE_CREDITS) {
    if (bluetoothActive) stopBluetooth();

    if (screenNeedsRedraw) {
      drawCredits();
      screenNeedsRedraw = false;
    }

    if (tap && backButtonHit(tx, ty)) currentMode = MODE_MENU;
  }
}
