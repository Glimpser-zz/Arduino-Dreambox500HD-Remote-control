# Arduino Dreambox 500HD Remote Control

An Arduino Mega 2560 that receives **three remotes** (Sony TV, Yamaha amplifier, Dreambox 500HD) with one IR receiver and sends to the three devices with one IR LED.
Selected keys of one remote are converted into commands for another device (for example Sony keys that the TV does not use control the Dreambox and the amplifier).
Every key without a rule is passed on unchanged to the device it belongs to, so the Arduino can also act as an IR repeater.

The Dreambox IR protocol used here is **not documented anywhere**. It was reverse engineered from the Dreambox kernel module
`dreambox_rc2.ko` (key tables, frame format) and checked against frames captured from the original remote. All extracted codes are in
[`data/`](data) and [`docs/`](docs).

## What it does

| Key received from | Has a rule in the table | No rule |
|---|---|---|
| Sony TV remote | converted: Yamaha (NEC) or Dreambox code | sent on to the **Sony TV** unchanged only when Repeat is ON: all received SIRC frames after release (40 kHz) |
| Yamaha remote | converted: Dreambox, Sony TV or another Yamaha code | sent on to the **Yamaha amplifier** unchanged only when Repeat is ON: the NEC frame and held-key repeats |
| Dreambox remote | converted: Yamaha, Sony TV or another Dreambox key | sent on to the **Dreambox** unchanged only when Repeat is ON: down, repeat and up pairs |

* Volume up / volume down are **continuous** on both devices: they repeat while the key is held. All other converted keys send one action.
* The rules are a table in the sketch (`RULES`). Each rule has the remote, the code, the target and whether the key is continuous. See [mapping](docs/sony-yamaha-mapping.md).
* **Version 3.1 menu:** the three independent Repeat switches control pass-through of **unmapped keys** from the Sony, Yamaha and Dreambox remotes. OFF suppresses the entire unmapped command: no initial frame and no repeat frames are sent. ON forwards the initial command and any repeats received. Rules in the conversion table are not affected. A separate **Climate control** switch disables/enables the complete summer/winter thermostat; OFF immediately de-energises both relays. These settings are stored in EEPROM.

### Two ways to place the Arduino

* **A. Next to the Yamaha amplifier and the Dreambox**, with their own IR windows covered, so that the devices only receive the Arduino's LED.
  Keep `FORWARD_SONY_REMOTE`, `FORWARD_YAMAHA_REMOTE` and `FORWARD_DREAMBOX_REMOTE` at `1`: the Arduino passes on every key of the three remotes.
* **B. Away from the devices**, in the direction where the Sony remote is used. The Yamaha and Dreambox remotes reach their devices directly, so set `FORWARD_YAMAHA_REMOTE` and `FORWARD_DREAMBOX_REMOTE` to `0`.
  Keys with a rule are still converted; the Sony remote's other keys are still passed on to the TV.

* All IR output uses a **hardware carrier from Timer5** (pin 45, `OC5B`): 38 kHz for the Dreambox and the Yamaha amplifier, 40 kHz for re-sent Sony codes.
* **Thermostat (version 3.1):** an AM2302 sensor, a 2-channel relay module and the LCD keypad shield turn the Arduino into a heating and cooling thermostat with a summer and a winter mode, a display of the temperature and humidity with their 24 hour minimum and maximum, and settings with the shield's buttons. See [docs/thermostat.md](docs/thermostat.md).

## Hardware

* Arduino Mega 2560
* KY-022 IR receiver module, signal on **pin 52**
* IR LED on **pin 45** (`OC5B` / Timer5), switched by a transistor (details and resistor values in [`docs/hardware.md`](docs/hardware.md))
* LCD keypad shield (DFRobot type), AM2302 sensor on pin 22, 2-channel relay module on pins 23 and 24 (version 3.0, see [`docs/hardware.md`](docs/hardware.md))

## Quick start

1. Wire the hardware as described in [`docs/hardware.md`](docs/hardware.md).
2. Open `src/ir_converter_smooth_serial/ir_converter_smooth_serial.ino` in the Arduino IDE, select **Arduino Mega or Mega 2560** and upload.
3. The sketch ships with `USE_SERIAL 1`: it prints every code it receives from the three remotes (115200 baud), which is how the codes for new rules are learned. Set it to `0` for stand-alone use from a power bank.
4. To change or add keys, edit the `RULES` table in the sketch. Dreambox key codes are listed in [`docs/dreambox-key-codes.md`](docs/dreambox-key-codes.md).

The first line of the sketch contains its version and build date.

## Repository layout

```
src/ir_converter_smooth_serial/   the converter, repeater and thermostat sketch (version 3.0)
tools/dreambox_decode/            prints the frames of an IR remote as hexadecimal words (use it to capture the original Dreambox remote)
tools/ir_rawdump/                 prints raw mark/space lengths of any IR remote
data/                             extracted Dreambox codes: CSV and text tables
docs/                             hardware, protocol, key codes, mapping, extraction method, development notes
```

## Documentation

* [Hardware and wiring](docs/hardware.md)
* [Thermostat: modes, display, settings](docs/thermostat.md)
* [Dreambox IR protocol](docs/dreambox-ir-protocol.md)
* [Dreambox key codes (extracted)](docs/dreambox-key-codes.md)
* [Sony to Yamaha / Dreambox mapping](docs/sony-yamaha-mapping.md)
* [How the codes were extracted](docs/how-the-codes-were-extracted.md)
* [Development notes: what worked and what did not](docs/development-notes.md)

## Status and honest limitations

* **Versions 2.0 and 3.0 have been tested in a simulator, and on the real devices only partly.** Versions up to 1.6 were tested on the TV, the Dreambox and the amplifier. With version 2.0 the Sony and Yamaha remotes worked on the author's setup, **the Dreambox remote did not**; the cause is not found yet (version 3.0 prints the raw durations of a Dreambox-like frame that it cannot decode, which will show why).
* The thermostat of version 3.0 (sensor, display, buttons, relays) has not been tested on the real hardware yet.
* In version 3.0 the IR LED was on pin 3 (`OC3C` / Timer3). The current sketch uses **pin 45** (`OC5B` / Timer5); pin 5 remains reserved for the LCD keypad shield.
* Yamaha remote: how it behaves when a key is held (full frames or short repeat codes) has not been captured. The sketch accepts both; a held key is passed on only after the remote has shown that it is held, so a tap stays a single frame.
* The Arduino does not receive while it transmits: a key pressed during a transmission (about 40 ms to 340 ms) is not seen.

* Tested on a real Dreambox 500HD: **Menu, Up, Down, Right, OK**. Two more codes (digit 0 and volume up) appear in captures of the original remote. All other Dreambox codes come from the module's key table and are **not tested yet**.
* Yamaha NEC codes were learned from the Yamaha remote itself.
* Re-sending unmapped Sony codes: versions 1.2 to 1.4 did not make the TV react. Version 1.5 sends the press the way the frequency scan did (see [`tools/ir_sony_freq_scan`](tools/ir_sony_freq_scan)), and the author confirmed that it **works**. The IR LED must point at the TV's IR window.
* Yamaha volume while a key is held: with short NEC repeat codes (up to 1.3) the amplifier did not keep changing the volume; since 1.4 the full NEC frame is repeated about every 120 ms, and the author confirmed that this works.
* The key-frame check digit and flag nibble were derived from the module and from three captured frames of one key. They match all captured frames; other keys use the same formula.
* This project is not affiliated with Dreambox, Sony or Yamaha. The kernel module itself is **not** included, only numbers derived from it.
