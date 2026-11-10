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
* for every burst: reset `TCNT3` to 0, connect `OC3A` by setting `COM3A1`, wait with `delayMicroseconds()`, disconnect, and drive the pin LOW with `digitalWrite()`;
* interrupts are blocked only while the timer registers are written.

The reason why the other hardware variants failed was not found.

## Sony re-send

Sony SIRC uses a 40 kHz carrier, 2400 us start burst and 600/1200 us bit bursts, each followed by 600 us. A 40 kHz re-send did not make the author's TV react
(the receiver light of the TV did not blink); the sketch now uses 38 kHz like everything else. Whether the TV reacts to the 38 kHz re-send has not been confirmed yet.

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
* **1.5** (not tested on the TV yet): unmapped Sony codes are re-sent after the key is released, as one blocking press of 3 frames started exactly 45 ms apart,
  the way the frequency scan sent them. The carrier is 40 kHz for these frames and returns to 38 kHz afterwards. Option `SONY_FORWARD_AFTER_RELEASE 0` starts at once and repeats while the key is held.
  A press of another mapped key that arrives while the Arduino is sending is not seen (the Arduino does not receive while it transmits).

## Sony re-send: what the tests showed

* **Loopback check** (`tools/ir_sony_loopback_check`): the Arduino sent one Sony frame (Mute, `0x094`) next to its own receiver at carriers from 36 to 42 kHz.
  The receiver saw start bursts of 2336 to 2428 us and bit bursts of 520 to 580 us (0) and 1152 to 1228 us (1) and read the value `0x094` correctly every time.
  The real Sony remote, seen by the same receiver, gave a start burst of 2404 us and bit bursts of 568 to 584 us and 1196 us. So the envelope of the generated signal matches the remote.
  The receiver only reports the envelope, not the carrier frequency of the real remote.
* **Frequency scan** (`tools/ir_sony_freq_scan`): with the IR LED 2 to 3 cm from the TV's IR window, the TV reacted to Mute at every carrier frequency from 36 to 42 kHz when the press was sent as 3 frames exactly 45 ms apart.
  So the carrier frequency within this range does not matter for this TV; geometry (the LED must point at the TV's IR window) and exact frame timing do.
