# Hardware

## Parts

| Part | Use |
|---|---|
| Arduino Mega 2560 | controller |
| KY-022 IR receiver module | receives the Sony remote (signal on pin 52) |
| IR LED, 5 mm, 940 nm class | transmitter, on pin 5 |
| NPN transistor (2N2222, BC337 ...) or logic-level MOSFET | switches the LED current |
| resistors | see below |

## Pins

| Pin | Function |
|---|---|
| 52 | IR receiver signal (KY-022) |
| 5 | IR LED driver, **Timer3 output OC3A**: the 38 kHz carrier comes from the hardware timer |
| 13 | built-in LED, on while a code is being sent |

Timer3 is a free 16-bit timer on the Mega, so `delay()`, `millis()` and `micros()` (Timer0) are not affected.
Do not use pin 4 for the LED: it belongs to Timer0.

## LED driver

The LED is switched on the low side. The Arduino pin only drives the base (or gate), the LED current flows from 5 V.

```
pin 5 ---- base resistor ---- base of NPN
5 V ---- LED series resistor ---- anode of the IR LED
cathode of the IR LED ---- collector
emitter ---- GND (common with the Arduino GND)
```

* **LED series resistor**: `R = (5 - Vf - Vsat) / I`, with `Vf` about 1.3 to 1.5 V for a 940 nm LED and `Vsat` about 0.2 V.
  * 220 ohm: about 15 mA. This is what the author uses with an unmarked 5 mm LED; range about 3 m.
  * 100 ohm: about 34 mA. 68 ohm: about 50 mA. Do not go below 68 ohm with an unmarked LED.
* **Base resistor** (computed, not necessarily the author's value): 330 to 470 ohm for a 2N2222 or BC337.

### Using an IRFZ44N instead

The IRFZ44N is **not** a logic-level MOSFET: its gate threshold is 2 to 4 V and it is fully on only at about 10 V. At 5 V it still works for an LED current of 100 mA or less, but it is not guaranteed. An IRLZ44N (logic level) or a small NPN is the better choice.
Pin order with the printed side facing you and the leads pointing down: gate, drain, source. The metal tab is the drain.

```
pin 5 ---- 100..220 ohm ---- gate
gate ---- 10 kohm ---- GND
5 V ---- LED resistor ---- LED anode
LED cathode ---- drain
source ---- GND
```

## Receiver

The KY-022 output idles HIGH and goes LOW during a carrier burst. The sketch measures these pulses with polling on pin 52.
Its small on-board light blinks while it receives IR, which is useful for checking that the transmitter LED is working.

## Range

Measured by the author with the unmarked LED, a transistor and 220 ohm: about 3 m. A direct line of sight to the receiver window of each device matters
more than current: a 5 mm LED has a narrow beam.
