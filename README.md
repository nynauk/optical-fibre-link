# Bidirectional optical fibre link for data and audio (ESP32)

My final-year project at Royal Holloway (BEng Computer Systems Engineering, dissertation graded 91%): two
near-identical ESP32 transceiver nodes that send framed binary data to each other over 1 m of plastic optical
fibre (a commercial TOSLINK cable), each with a 650 nm laser diode transmitter, a discrete photodiode receiver
chain and a touchscreen. This repository holds the ESP32 firmware and the PCB Gerber files. The design and the
measurements are written up in the dissertation; the main results are summarised below.

## At a glance

- **Hardware.** Transmitter: a KY-008 650 nm laser module switched by a 2N2222A transistor from an ESP32 GPIO.
  Receiver: a Vishay BPW34 PIN photodiode into an OPA356 transimpedance amplifier (R_f = 22 kΩ, C_f = 2.2 pF)
  and a TLV3501 comparator with about 300 mV of Schmitt hysteresis around a 2.0 V reference. A two-layer PCB
  carrying both nodes was laid out in EasyEDA and fabricated by JLCPCB.
- **Data rate.** With the fibre held by hand in 3D-printed adapters, the receiver counted edges correctly to
  within 0.005% at 8, 64 and 128 kbps over about ten minutes at each rate. 384 and 512 kbps worked
  intermittently, and 640 kbps held for about two seconds under the best alignment.
- **Packet integrity.** Runs of 1,000 framed packets with 16-byte payloads had zero frame errors at 8 and 64 kbps
  and a few errors per thousand at 128 kbps, almost all from the fibre end drifting during hand alignment.
- **Main lesson.** The analogue chain was not the limit: the compensated amplifier's bandwidth is about 3.3 MHz,
  more than five times the 640 kbps peak. Mechanical alignment between the fibre and the photodiode, ambient
  light, and comparator spikes that I traced to poor SOIC solder joints were.

## How it works

```
 Node A                                                                Node B
 ESP32 GPIO25 ─► 2N2222A ─► KY-008 laser ═══ 1 m TOSLINK fibre ═══► BPW34 ─► OPA356 TIA ─► TLV3501 ─► ESP32 GPIO34
 ESP32 GPIO34 ◄─ TLV3501 ◄─ OPA356 TIA ◄─ BPW34 ◄═══ 1 m TOSLINK fibre ═══ KY-008 laser ◄─ 2N2222A ◄─ ESP32 GPIO25
```

**Modulation.** On-off keying: laser on for a 1, off for a 0. Each byte is sent with a start bit and a stop bit,
and the receiver samples in the middle of each bit period.

**Framing.** Above the byte level, every message is a packet:

| Field | Size | Content |
|---|---|---|
| Start | 1 byte | `0x7E` |
| Length | 1 byte | number of payload bytes |
| Payload | 0 to 255 bytes | the data |
| Check | 1 byte | XOR of the length byte and every payload byte |

The receiver (`parseRxBytes` in the integrated firmware) is a four-state machine: wait for `0x7E`, read the
length, read the payload while accumulating the XOR, then accept the packet only if the check byte matches. The
XOR check catches any single-bit error, or any odd number of flipped bits in the same bit position.

**Threshold choice.** The 2.0 V comparator reference came from a bench sweep from 0.51 V to 2.49 V at a constant
10 kHz drive. Below about 1.5 V amplifier noise made the comparator chatter in the dark half of the cycle; above
about 2.3 V it started to miss roughly one edge in 250. 2.0 V sits in the middle of that window.

**Rate measurement.** For the data-rate characterisation the receiver used the ESP32's PCNT hardware pulse counter
on the comparator output, read every 100 ms with a 1 µs glitch filter, so the bit rate could be measured
without a frequency counter or logic analyser.

## Firmware

| Sketch | Purpose |
|---|---|
| `00_final_integrated_transceiver` | The full node: touchscreen menu, framed binary send and receive, a live photodiode monitor and a Bluetooth A2DP audio mode |
| `08_final_integrated_transceiver_threshold4000_variant` | The same program with the receiver ADC threshold raised from 1500 to 4000 |
| `01_low_speed_bidirectional_10Hz_threshold4000` | Two identical nodes exchanging data at 10 bits a second, for first bring-up |
| `02_laser_1Hz_blink_test`, `03_laser_pwm_speed_test` | Laser drive checks, including switching at set frequencies |
| `04_receiver_frequency_counter` | Edge counting on the comparator output with an interrupt, by count and by period |
| `05_bpw34_analog_live_monitor`, `06_comparator_digital_monitor` | Photodiode level on the ADC, and the comparator output as a digital input |
| `07_bluetooth_a2dp_i2s_test` | Bluetooth A2DP into I2S for the audio path |

The integrated firmware runs the link at a 750 µs bit period, about 1.3 kbps, which is enough for interactive
text between the touchscreens. The higher-rate characterisation and the 1,000-packet runs described above used
separate transmitter, receiver and packet-test sketches with the PCNT counter; those are described in the
dissertation and are not in this repository.

### Pin map

| Function | ESP32 pin |
|---|---:|
| Laser output | GPIO 25 |
| Photodiode / comparator input | GPIO 34 |
| TFT CS, DC, RST | GPIO 5, 2, 4 |
| TFT SCK, MISO, MOSI | GPIO 18, 19, 22 |
| Touch CS, IRQ | GPIO 21, 26 |
| TFT backlight | GPIO 32 |
| I2S BCLK, LRC, DOUT | GPIO 27, 14, 23 |

### Build

Arduino IDE with the ESP32 board package and these libraries: Adafruit GFX, Adafruit ILI9341,
XPT2046_Touchscreen, AudioTools and ESP32-A2DP (`BluetoothA2DPSink`). Open a sketch, select an ESP32 Dev Module,
upload over USB, and use the serial monitor at 115200 baud.

## Hardware files

`hardware/gerbers/` holds the two-layer board as exported from EasyEDA on 26 April 2026 for JLCPCB. The bottom
layer is an analogue ground plane under the photodiode, amplifier and comparator, the feedback resistor and
capacitor sit as close to the OPA356 input as the footprint allows, and test points are marked on the
silkscreen at the reference voltage and the comparator output. Two earlier receiver breakout boards failed (a
broken trace on the hysteresis return, and a ground routing error that destroyed a TLV3501 at power-up), and
those failures are why the final layout has explicit hand-checked ground paths and test points at every
analogue node.

## Limitations and next steps

- Alignment was by hand, so the rates above 128 kbps are not repeatable. A clamped mechanical reference and a
  proper bit-error-rate sweep on the populated PCB are the next experiments.
- The photodiode runs at zero bias. Reverse-biasing it would lower its capacitance and roughly raise the
  amplifier bandwidth from about 7 MHz to about 11 MHz, but the board has no 5 V rail at that point.
- Bluetooth audio played a swept tone clearly, but streamed music was not yet intelligible, and audio over the
  optical link itself was not finished.
- Replacing the KY-008 laser module with a VCSEL, for faster rise times, is the planned transmitter upgrade.
