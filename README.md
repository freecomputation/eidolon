# Eidolon

A ghost hunting instrument kit for the Flipper Zero.

Eidolon (Greek eidolon: an apparition, the spirit-image of a person) turns a
Flipper Zero into a carried investigation instrument. Every reading it takes
is a real measurement from the Flipper's own radios and receivers, and every
event is written to a timestamped investigation log on the SD card so a
session can be reviewed afterwards instead of remembered.

Instruments:

- Field meter - watches the sub-GHz RF noise floor with the CC1101 receiver.
  A baseline is set for the room, and deviations raise a tone whose pitch
  follows the energy, Geiger-counter style.
- Dark watch - listens on the infrared receiver for IR events in darkness:
  illuminators, camera flashes, remote bursts.
- Proximity sense - reads disturbance of the 125 kHz field around the LF
  antenna when something comes close.
- Investigation log - the spine of the kit. All instruments append
  timestamped events to a session file under the app's data folder.

Stated limits: a Flipper Zero has no microphone, no magnetometer and no
temperature sensor, so Eidolon claims none of those instruments. It measures
radio energy, infrared light and field disturbance, and reports exactly
that.

Target: official Flipper Zero firmware 1.5.1 (firmware API 88.2). The
ESP32-S2 WiFi dev board is not required; a WiFi census module is planned
once it can be driven reliably.

License: AGPL-3.0.
