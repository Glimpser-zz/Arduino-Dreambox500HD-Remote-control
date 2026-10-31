# Development notes

What was learned while getting this to work. These are observations, not all of them are explained.

## Things that matter

* **Serial printing disturbs IR timing.** The Dreambox needs every interval inside a window of about 136 us. A serial interrupt stretches the gaps between bursts.
  The sketch calls `Serial.flush()` (through `LOG()`) before every transmission.
* **A single key press is a sequence**: down pair, repeat pairs, up pair. The sketch sends two repeat pairs for a single key; this exact sequence was tested.
* **Hold behaviour**: continuous keys send down, then a repeat pair every 118 ms, then up when no Sony frame has arrived for 200 ms.
  Yamaha continuous keys send the NEC frame, then NEC repeat codes every 108 ms.
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
