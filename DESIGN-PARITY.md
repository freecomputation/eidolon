# Eidolon - competitive parity spec (2026-11-10)

Chase's ruling: Eidolon does everything Spectre and Ghost Tracker can do, and more.
Sources: github.com/rseufert/spectre (README), github.com/Zinetick/ghost_tracker
(README + official Flipper catalog manifest, applications/Tools/ghost_tracker).

## What they have

Ghost Tracker (Zinetick) - all real hardware, no RNG theater:
- EMF Meter internal: CC1101 round-robin over 8 fixed freqs 300-348 MHz, real RSSI.
- EMF Meter external: SS49E Hall sensor on pin 16 (PC0 ADC), 640 Hz sampling,
  Goertzel filter at 50/60 Hz mains hum, K-II style. Pin 9 = 3V3, pin 11 = GND.
- Spirit Box: real Sub-GHz sweep 300-348 MHz, dwell 100-500 ms adjustable,
  raw demodulated RX bitstream mirrored to the speaker pin in hardware
  (furi_hal_subghz_set_async_mirror_pin) - genuine RF static; RSSI threshold
  crossing flashes SEÑAL + LED; OK = mute.
- REM Pod: bare copper wire (~15 cm) on pin 15 (PC1 ADC), floating high-Z node,
  24 raw back-to-back ADC samples per tick, peak-to-peak jitter = signal;
  sensitivity Left/Right, alert volume Up/Down; alert = LED strobe + tone.
- In-app wiring reference. No logging anywhere. TEA5767 FM support researched
  but NOT implemented (key finding: FM audio cannot route to the Flipper
  speaker; the Flipper would do I2C tuning only, audio via a separate speaker).

Spectre (rseufert) - one shared spirit-energy level, four tabs:
- SENSE: needle gauge QUIET to CONTACT. Real inputs: NFC and 125 kHz reader
  field detect push it hard; battery temperature DROP since start = COLD SPOT.
  Geiger crackle quickens as needle climbs. OK = reset baseline and peak.
- BOX: spirit box sweeping a Sub-GHz band in 64 steps, noise floor drawn as a
  ribbon, channel hopping as static bursts; on a real on-air lock the tone
  wavers and a word comes through. Bands 300 / 433 / 868 (Up/Down), OK = hold.
- VOICE: words arrive on energy surges, spelled letter by letter with a garbled
  syllable each; OK = ask a question; last 3 words stay on screen.
- RADAR: sweeping scope, drifting echoes, nearest echo range + bearing, ping
  as the beam crosses; OK = yes/no answer.
- Surge feedback: screen flash, buzzer, LED breathes in the tab color, scaled
  by energy. Hold OK = sound on/off. Receive-only; honest toy disclaimer
  (energy = real inputs + hardware RNG).

## Eidolon parity (adopts all of the above)

- Field meter: CC1101 round-robin 300-348 MHz RSSI (Ghost Tracker internal)
  plus single-band noise floor, needle gauge, baseline zero, peak-hold,
  Geiger tone (Spectre SENSE), plus NFC / 125 kHz reader-field detect as
  needle inputs (Spectre), plus battery-temperature cold-spot trend (Spectre).
- Hall mode: same optional SS49E wiring as Ghost Tracker (pin 16, Goertzel
  50/60 Hz) - same pin map, so one sensor serves both apps.
- Spirit box: real raw demodulated static via the async mirror pin
  (Ghost Tracker technique), sweep 300-348 MHz, dwell adjustable, RSSI
  threshold LED flash - plus the 64-step noise-floor ribbon and a word cue
  on genuine lock-on (Spectre BOX).
- REM pod: Ghost Tracker's method exactly - copper wire pin 15 ADC jitter,
  sensitivity and volume controls, LED strobe + tone.
- Voice: word events on measured surges, spelled out, ask-a-question,
  last 3 words (Spectre VOICE), labeled honestly in-app as environment-fed
  theater, same as Spectre's own disclaimer.
- Radar: sweep scope, echoes, nearest range/bearing, ping, yes/no
  (Spectre RADAR).
- Surge feedback: screen flash + buzzer + LED breathing scaled by level.
  Hold OK = sound toggle. Same control language as Spectre where sensible.

## What neither competitor has - Eidolon's "more"

- Investigation log: every event from every instrument, timestamped, written
  to the SD card as a session file. Neither competitor logs anything.
- Dark watch: infrared receiver event log (illuminators, camera flashes).
  Neither competitor uses the IR receiver as a sensor.
- Fusion watch: all instruments at once on one timeline with a combined
  presence level - field, REM, IR, cold-spot trend together, all logged.
- WiFi census through the dev board once its seat fault is found: the room's
  access points and clients logged on the same investigation timeline.
- TEA5767 readiness: Ghost Tracker only planned it. When a module is in hand,
  Eidolon ships the I2C tuner control with the external-speaker plan.

## Build order

Logger spine first, then field meter, spirit box, REM pod, dark watch,
voice, radar, fusion watch - landing each by PR.
