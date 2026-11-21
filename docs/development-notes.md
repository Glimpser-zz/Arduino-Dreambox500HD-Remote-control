# Development notes

What was learned while getting this to work. These are observations, not all of them are explained.

## Things that matter

* **Serial printing disturbs IR timing.** The Dreambox needs every interval inside a window of about 136 us. A serial interrupt stretches the gaps between bursts.
  The sketch calls `Serial.flush()` (through `LOG()`) before every transmission.
* **A single key press is a sequence**: down pair, repeat pairs, up pair. The sketch sends two repeat pairs for a single key; this exact sequence was tested.
* **Hold behaviour**: continuous keys send down, then a repeat pair every 118 ms, then up when no Sony frame has arrived for 200 ms.
  Yamaha continuous keys send the NEC frame and then, in version 1.4, the full frame again every 120 ms (up to 1.3: short NEC repeat codes every 108 ms; the amplifier did not keep changing the volume with them).
* **Single keys are sent after the Sony key is released**, so the Sony remote's own IR does not overlap with the transmission.
* Receiving: the KY-022 can stretch marks; the sketch uses the midpoint between the shortest and the longest mark of a frame as the 0/1 threshold for Sony codes.
  The start mark window is 1500 to 3500 us.

## Hardware carrier

A software-toggled carrier worked on the Dreambox. Several hardware-timer attempts did **not** work although the KY-022 module showed that the LED was emitting:
different carrier frequencies from 35 to 40 kHz, different mark lengths from 200 to 600 us, and a variant that preset the timer counter to its TOP value before each burst.
A sketch written with this pattern **did** work and is what the converter uses:

* configure Timer3 in fast PWM mode 14 (TOP = ICR3, no prescaler), duty about 1/3, with the output disconnected;
* for every burst: reset `TCNT3` to 0, connect the output (`OC3A` up to version 2.0, `OC3C` on pin 3 since 3.0) by setting its `COM3x1` bit, wait with `delayMicroseconds()`, disconnect, and drive the pin LOW with `digitalWrite()`;
* interrupts are blocked only while the timer registers are written.

The reason why the other hardware variants failed was not found.

## Sony re-send

Sony SIRC uses a 40 kHz carrier, 2400 us start burst and 600/1200 us bit bursts, each followed by 600 us. A 40 kHz re-send did not make the author's TV react
(the receiver light of the TV did not blink); versions 1.3 and 1.4 used 38 kHz and the TV did not react either. The carrier was not the cause (see the findings below and version 1.5): what made the difference was the exact 45 ms frame timing, no overlap with the original remote, and the LED pointing at the TV.

## Unexplained

* While debugging, the built-in LED showed that a key tap on the Sony remote was sometimes followed by a second received Sony code that had no rule.
  Such codes are now forwarded to the TV like any other unmapped code.

## Version history

* **1.1**: new key table, version line.
* **1.2**: Blue and Red mapped to Dreambox Down and Up; unmapped Sony codes re-sent with a 40 kHz carrier, one frame per received frame. The TV did not react.
* **1.3**: unmapped Sony codes re-sent with the 38 kHz carrier. The TV still did not react. Yamaha volume did not change continuously while the key was held (NEC repeat codes); Dreambox volume (CH+ / CH-) did.
* **1.4**: Yamaha hold repeats the full NEC frame about every 120 ms (`NEC_HOLD_FULL_FRAME`, set it to 0 for short repeat codes). **Confirmed working** by the author.
  Unmapped Sony codes were sent at least three times, 45 ms apart, and kept going while the original key was held. The TV still did not react.
  A simulator run shows about 135 ms between full NEC frames and 45 ms between Sony frames, with occasional gaps of 90 ms while the Arduino is busy receiving.
* **1.5** (**confirmed working** on the TV by the author): unmapped Sony codes are re-sent after the key is released, as one blocking press of 3 frames started exactly 45 ms apart,
  the way the frequency scan sent them. The carrier is 40 kHz for these frames and returns to 38 kHz afterwards. Option `SONY_FORWARD_AFTER_RELEASE 0` starts at once and repeats while the key is held.
  A press of another mapped key that arrives while the Arduino is sending is not seen (the Arduino does not receive while it transmits).

* **1.6**: functions of Sony Green and CH+ swapped, and of Sony Yellow and CH- swapped. Green and Yellow are now the continuous Dreambox volume up and down; CH+ and CH- send Dreambox Right and Left (single actions).

* **2.0** (simulator only, not tested on the devices yet): the sketch receives the Yamaha remote (NEC) and the Dreambox remote as well as the Sony remote, and rules can map any of them to any device (`IN_SONY`, `IN_NEC`, `IN_DREAMBOX` to `OUT_DREAMBOX`, `OUT_NEC`, `OUT_SONY`).
  Keys without a rule are passed on to their own device. Switches `FORWARD_SONY_REMOTE`, `FORWARD_YAMAHA_REMOTE`, `FORWARD_DREAMBOX_REMOTE` turn the passing on or off per remote (placement B in the README).

* **3.0** (simulator only): thermostat added (AM2302 sensor, 2-channel relay module, LCD keypad shield, EEPROM settings, 24 hour history; see [thermostat.md](thermostat.md)).
  The IR LED moved from pin 5 (`OC3A`) to pin 3 (`OC3C`) because the LCD shield uses pin 5. The sensor and the display are touched only while the IR part is idle. A Dreambox-like frame that cannot be decoded is printed (durations) with `USE_SERIAL 1`.

## Sony re-send: what the tests showed

* **Loopback check** (`tools/ir_sony_loopback_check`): the Arduino sent one Sony frame (Mute, `0x094`) next to its own receiver at carriers from 36 to 42 kHz.
  The receiver saw start bursts of 2336 to 2428 us and bit bursts of 520 to 580 us (0) and 1152 to 1228 us (1) and read the value `0x094` correctly every time.
  The real Sony remote, seen by the same receiver, gave a start burst of 2404 us and bit bursts of 568 to 584 us and 1196 us. So the envelope of the generated signal matches the remote.
  The receiver only reports the envelope, not the carrier frequency of the real remote.
* **Frequency scan** (`tools/ir_sony_freq_scan`): with the IR LED 2 to 3 cm from the TV's IR window, the TV reacted to Mute at every carrier frequency from 36 to 42 kHz when the press was sent as 3 frames exactly 45 ms apart.
  So the carrier frequency within this range does not matter for this TV; geometry (the LED must point at the TV's IR window) and exact frame timing do.

## Design notes for version 2.0

* **Classification of a received frame** is by its first burst: about 2400 us = Sony, about 9000 us = NEC (a full frame has 67 or more durations, a repeat code 3), under 700 us with 17 durations = Dreambox.
* **Dreambox remote**: a key is a header frame and a key frame, only about 13 ms apart. The sketch does not print or transmit between the two, otherwise it would miss the key frame.
  The key frame is decoded with the module's own rules (interval to nibble, nibble sum multiple of 16, mask `0xC06C80FF`). An up pair ends the key at once; without it the key ends 300 ms after the last pair.
  A forwarded key keeps the header of the remote, then follows the normal hold engine: down pair, repeat pairs every 118 ms, up pair.
* **Yamaha remote**: a tap sends one frame, so a forwarded key is repeated only after the remote has been seen again (a second frame or a repeat code). The sketch cannot see a repeat code that arrives while it is transmitting (a full frame takes 68 ms),
  so the first repeat of a held key is usually missed and the amplifier receives the second frame about 160 ms after the first, then every 120 ms. How the real remote behaves when held has not been captured.
* **The Arduino does not receive while it transmits.** Own transmissions are not mistaken for a key, because the receiver is not listened to while sending.
* **Release timeouts** (no frame of the key for this long): Sony remote 120 ms (single) / 200-220 ms (continuous), Yamaha and Dreambox remotes 300 ms (their frames come only every 108 to 118 ms).
