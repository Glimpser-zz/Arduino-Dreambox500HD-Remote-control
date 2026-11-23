# Hardware

## Parts

| Part | Use |
|---|---|
| Arduino Mega 2560 | controller |
| KY-022 IR receiver module | receives the three remotes (signal on pin 52) |
| IR LED, 5 mm, 940 nm class | transmitter, on pin 45 (`OC5B` / Timer5) |
| NPN transistor (2N2222, BC337 ...) or logic-level MOSFET | switches the LED current |
| resistors | see below |
| LCD keypad shield (DFRobot type, 16 x 2 LCD and 5 buttons) | display and settings of the thermostat (version 3.0) |
| AM2302 (DHT22 type) temperature and humidity sensor | thermostat sensor (version 3.0) |
| 2-channel 5 V relay module | heating and cooling outputs (version 3.0) |

## Pins

| Pin | Function |
|---|---|
| 52 | IR receiver signal (KY-022) |
| 45 | IR LED driver, **Timer5 output OC5B**: the carrier comes from the hardware timer |
| 13 | built-in LED, on while a code is being sent |
| 4, 5, 6, 7, 8, 9 | LCD keypad shield: D4, D5, D6, D7, RS (8) and EN (9); in the sketch `LiquidCrystal lcd(8, 9, 4, 5, 6, 7)` |
| 10 | LCD backlight on the shield (the sketch leaves it alone) |
| A0 | shield buttons, one analog input: RIGHT about 0, UP about 144, DOWN about 329, LEFT about 504, SELECT about 741, nothing about 1023 |
| 22 | AM2302 data |
| 23 | relay module IN1: heating |
| 24 | relay module IN2: cooling |

**The IR LED output is currently on pin 45 (`OC5B` / Timer5).** The earlier version 3.0 wiring used pin 3 (`OC3C` / Timer3); pin 5 is reserved for the LCD keypad shield's D5 line.
On the Mega 2560, Timer5 outputs are pin 46 (`OC5A`), pin 45 (`OC5B`) and pin 44 (`OC5C`). This sketch uses the hardware output on pin 45. Timer5 is a free 16-bit timer in this project; `delay()`, `millis()` and `micros()` (Timer0) are not affected.
Do not use pins 44 or 46 for other functions while the Timer5 carrier output is configured; pin 45 is the IR LED driver output.

## LED driver

The LED is switched on the low side. The Arduino pin only drives the base (or gate), the LED current flows from 5 V.

```
pin 45 ---- base resistor ---- base of NPN
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
pin 45 ---- 100..220 ohm ---- gate
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

## LCD keypad shield

The shield is a 16 x 2 LCD with five buttons (UP, DOWN, LEFT, RIGHT, SELECT) and a RESET button.
It plugs into the Arduino headers; on a Mega it covers digital pins 0 to 13 and A0 to A5. If the shield is not stacked, wire these lines instead:
5 V, GND, pins 4, 5, 6, 7, 8, 9 and A0 to the matching shield pads. The blue trimmer on the shield sets the LCD contrast; turn it if the text is invisible or all blocks are dark.
Every pin of the shield stays free for other uses except 4 to 10 and A0.

## AM2302 (DHT22) temperature and humidity sensor

Pins of the 4-pin sensor with the grid side facing you, from left to right: **1 VDD (5 V), 2 DATA, 3 not connected, 4 GND**. The data line needs a pull-up resistor of 10 kohm to 5 V (many sensor boards already have it).

```
sensor pin 1 ---- 5 V
sensor pin 2 ---- Arduino pin 22, and 10 kohm to 5 V
sensor pin 4 ---- GND
```

The sensor must not be read more often than every 2 seconds; the sketch reads it every 5 seconds, and only while no remote key is being handled (a reading blocks the Arduino for about 5 ms).
Keep the sensor away from the heater, the cooling unit and the Arduino itself, and out of direct sun or air streams: its reading is what the thermostat controls.

## 2-channel relay module

* Module inputs: `VCC` to 5 V, `GND` to GND, `IN1` to pin 23 (heating), `IN2` to pin 24 (cooling).
* The usual module switches a relay **on when its input is LOW**. The sketch assumes this (`RELAY_ACTIVE_LOW 1`); if your module is the other kind, set it to 0.
* With the jumper `JD-VCC` / `VCC` fitted (as on the module in the photo), the relay coils are powered from the Arduino's 5 V. Without the jumper, power `JD-VCC` from a separate 5 V supply to isolate the Arduino completely.
* The relays are marked 10 A at 250 VAC / 30 VDC. Each relay has three terminals: `COM` (common), `NO` (normally open: connected to COM while the relay is on) and `NC` (normally closed).
  Use `COM` and `NO` to switch the device, in series with its supply or, better, with its thermostat or control input.

**Mains voltage is dangerous.** Wiring a heater, an air conditioner or any device to 230 V must be done by a qualified person, in a closed enclosure, with the correct fuse and wire size.
A heater should also have its own independent safety thermostat or limiter: this project is a convenience thermostat, not a safety device.
For a heavy load (large heater, compressor) switch a contactor, not the load itself, with the relay.

## Power

A rough budget: the Mega about 100 mA, the LCD shield with its backlight about 100 mA, the relay module about 70 mA per energised relay, the receiver a few mA, and the IR LED peaks of 30 to 50 mA.
That is up to about 400 mA. A USB port that is limited to 500 mA is at its limit: use a 5 V supply of 1 A or more, or a power bank that can deliver it.
