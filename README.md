# Eidolon

A ghost hunting instrument kit for the Flipper Zero.

Eidolon (Greek eidolon: an apparition, the spirit-image of a person) turns a
Flipper Zero into a carried investigation instrument. Every reading it takes
is a real measurement from the Flipper's own radios and receivers, and every
event is written to a timestamped investigation log on the SD card so a
session can be reviewed afterwards instead of remembered.

Target: official Flipper Zero firmware 1.5.1 (firmware API 88.2), built
with ufbt (see SETUP.md). Building produces `dist/eidolon.fap`; copy it to
`apps/Tools` on the SD card.

## Instruments

- Field meter - the CC1101 Sub-GHz receiver as an RF field meter. Modes:
  an 8-frequency RSSI sweep across 300-348 MHz, a single band with 100 kHz
  steps, and a Hall mode (below). Needle gauge with peak hold and a history
  strip, OK zeroes the baseline and the peak, Geiger clicks quicken with
  level. NFC and 125 kHz reader fields are detected passively and push the
  needle. A drop in fuel-gauge battery temperature is shown and logged as
  a cold spot.
- Hall mode - an optional SS49E linear Hall sensor on pin 16, burst-sampled
  and run through a Goertzel filter at 50 and 60 Hz: the mains-hum quantity
  a K-II meter reports. Wiring: OUT to pin 16 (PC0, ADC), VCC to pin 9
  (3.3V), GND to pin 11.
- Spirit box - a real Sub-GHz sweep (300-348, 387-464, 779-928 MHz, 64
  steps, 100-500 ms dwell). The CC1101's raw demodulated RX bitstream is
  mirrored onto the speaker pin in hardware, so the audio is genuine RF
  static at each step, not a synthesized tone. A 64-step ribbon shows the
  noise floor per step; a carrier standing 14 dB over the floor across
  repeated passes is logged as a lock-on and cues a word. OK mutes.
- REM pod - a bare copper wire (about 15 cm) on pin 15 (PC1, ADC) leaves a
  floating high-impedance node coupled to the ambient mains field; a nearby
  body changes that coupling. The signal is the peak-to-peak jitter of 24
  back-to-back raw ADC samples. A slowly tracking baseline keeps a
  merely-present wire quiet. Sensitivity and alert volume are adjustable;
  an alert strobes the LED, sounds a tone, and is logged.
- Dark watch - the infrared receiver, watched in the dark. Every burst
  (remotes, camera illuminators and flashes) is logged with protocol,
  address and command when it decodes, or its raw timing count when it
  does not. OK writes a manual mark.
- Voice - word events driven by measured surges in the other instruments
  plus the hardware RNG, spelled out letter by letter with a syllable blip
  each. OK asks a question; the last three words stay on screen; every
  completed word is logged.
- Radar - a sweeping scope. Echo appearance is weighted by the same
  measured activity; the nearest echo's range and bearing are read out,
  the beam pings as it crosses, and OK asks a yes/no question.
- Fusion watch - all sensors sampled together: Sub-GHz field, REM jitter,
  infrared events, reader fields, battery-temperature trend, combined
  into one presence level and logged on one timeline (a summary every
  15 s, an event on contact). OK re-zeroes the baselines.
- Investigation log - the spine of the kit. A session file
  (`session-YYYYMMDD-HHMMSS.log`) in the app's data folder on the SD card
  (`apps_data/eidolon`); every event carries the RTC clock time and the
  session uptime.

## Honesty

The meters are real measurements and are never faked: RF power, ADC
jitter, IR bursts, reader fields, battery temperature. The voice and
radar are theater fed by those measurements plus the hardware RNG - what
is real is *when* surges trigger them, never which word or answer comes
out. The in-app About screen states the same, with the pin map and
controls.

Stated limits: a Flipper Zero has no microphone and no magnetometer, so
Eidolon claims neither an EVP recorder nor a magnetic-field meter (the
Hall mode reads an external wired sensor). A planned WiFi census through
the ESP32-S2 dev board is not included; the kit has no dependency on it.
DESIGN-PARITY.md records how the instrument set was scoped against the
existing Spectre and Ghost Tracker apps.

## Controls

- Menu: Up/Down to choose, OK to open, Back to exit the app.
- Hold OK anywhere: sound on/off.
- Per-instrument controls are listed on each screen and in About.

License: AGPL-3.0.
