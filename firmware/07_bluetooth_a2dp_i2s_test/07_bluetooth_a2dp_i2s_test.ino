#include <Arduino.h>
#include "AudioTools.h"
#include "BluetoothA2DPSink.h"

#define I2S_BCLK 27
#define I2S_LRC  14
#define I2S_DOUT 23

I2SStream i2s;
BluetoothA2DPSink a2dp_sink(i2s);

void setup() {
  Serial.begin(115200);

  auto cfg = i2s.defaultConfig(TX_MODE);
  cfg.sample_rate = 44100;
  cfg.bits_per_sample = 16;
  cfg.channels = 2;
  cfg.buffer_size = 1024;
  cfg.buffer_count = 8;
  cfg.pin_bck = I2S_BCLK;
  cfg.pin_ws = I2S_LRC;
  cfg.pin_data = I2S_DOUT;

  i2s.begin(cfg);
  a2dp_sink.set_auto_reconnect(false);
  a2dp_sink.start("NYNA_AUDIO");

  Serial.println(F("Bluetooth A2DP sink started. Pair with: NYNA_AUDIO"));
}

void loop() {
  delay(1000);
}
