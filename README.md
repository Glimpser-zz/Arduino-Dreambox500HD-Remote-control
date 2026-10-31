# Arduino Dreambox 500HD Remote Control

An Arduino Mega 2560 that sits in front of a Sony TV remote and turns it into a remote for a **Dreambox 500HD** and a **Yamaha amplifier**.
Keys that the TV does not use are translated to Dreambox or Yamaha commands. Every other Sony code is sent on to the TV unchanged.

The Dreambox IR protocol used here is **not documented anywhere**. It was reverse engineered from the Dreambox kernel module
`dreambox_rc2.ko` (key tables, frame format) and checked against frames captured from the original remote. All extracted codes are in
[`data/`](data) and [`docs/`](docs).

## What it does

| Sony remote sends | Arduino sends |
|---|---|
| a key that has a rule (see [mapping](docs/sony-yamaha-mapping.md)) | the matching Yamaha (NEC) or Dreambox code |
| a Sony code without a rule | the same Sony code, unchanged |

* Volume up / volume down are **continuous** on both devices: they repeat while the Sony key is held. All other keys send one action.
* All IR output uses a **hardware 38 kHz carrier from Timer3** (pin 5, `OC3A`).

## Hardware

* Arduino Mega 2560
* KY-022 IR receiver module, signal on **pin 52**
* IR LED on **pin 5**, switched by a transistor (details and resistor values in [`docs/hardware.md`](docs/hardware.md))

## Quick start

1. Wire the hardware as described in [`docs/hardware.md`](docs/hardware.md).
2. Open `src/ir_converter_smooth_serial/ir_converter_smooth_serial.ino` in the Arduino IDE, select **Arduino Mega or Mega 2560** and upload.
3. The sketch ships with `USE_SERIAL 1`: it prints every Sony code it receives (115200 baud). Set it to `0` for stand-alone use from a power bank.
4. To change or add keys, edit the `RULES` table in the sketch. Dreambox key codes are listed in [`docs/dreambox-key-codes.md`](docs/dreambox-key-codes.md).

The first line of the sketch contains its version and build date.

## Repository layout

```
src/ir_converter_smooth_serial/   the converter sketch (Sony -> Dreambox / Yamaha, pass-through of the rest)
tools/dreambox_decode/            prints the frames of an IR remote as hexadecimal words (use it to capture the original Dreambox remote)
tools/ir_rawdump/                 prints raw mark/space lengths of any IR remote
data/                             extracted Dreambox codes: CSV and text tables
docs/                             hardware, protocol, key codes, mapping, extraction method, development notes
```

## Documentation

* [Hardware and wiring](docs/hardware.md)
* [Dreambox IR protocol](docs/dreambox-ir-protocol.md)
* [Dreambox key codes (extracted)](docs/dreambox-key-codes.md)
* [Sony to Yamaha / Dreambox mapping](docs/sony-yamaha-mapping.md)
* [How the codes were extracted](docs/how-the-codes-were-extracted.md)
* [Development notes: what worked and what did not](docs/development-notes.md)

## Status and honest limitations

* Tested on a real Dreambox 500HD: **Menu, Up, Down, Right, OK**. Two more codes (digit 0 and volume up) appear in captures of the original remote. All other Dreambox codes come from the module's key table and are **not tested yet**.
* Yamaha NEC codes were learned from the Yamaha remote itself.
* Re-sending unmapped Sony codes is the least tested part. The Sony standard carrier is 40 kHz, but the sketch uses 38 kHz for everything; the author's TV did not react to a 40 kHz re-send.
* The key-frame check digit and flag nibble were derived from the module and from three captured frames of one key. They match all captured frames; other keys use the same formula.
* This project is not affiliated with Dreambox, Sony or Yamaha. The kernel module itself is **not** included, only numbers derived from it.
