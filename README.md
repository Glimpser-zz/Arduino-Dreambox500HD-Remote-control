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
| Sony TV remote | converted: Yamaha (NEC) or Dreambox code | sent on to the **Sony TV** unchanged (one press of 3 frames, 45 ms apart, 40 kHz, after the key is released) |
| Yamaha remote | converted: Dreambox, Sony TV or another Yamaha code | sent on to the **Yamaha amplifier** unchanged (NEC frame, repeated while the key is held) |
| Dreambox remote | converted: Yamaha, Sony TV or another Dreambox key | sent on to the **Dreambox** unchanged (down pair, repeat pairs every 118 ms, up pair) |

* Volume up / volume down are **continuous** on both devices: they repeat while the key is held. All other converted keys send one action.
* The rules are a table in the sketch (`RULES`). Each rule has the remote, the code, the target and whether the key is continuous. See [mapping](docs/sony-yamaha-mapping.md).

### Two ways to place the Arduino

* **A. Next to the Yamaha amplifier and the Dreambox**, with their own IR windows covered, so that the devices only receive the Arduino's LED.
  Keep `FORWARD_SONY_REMOTE`, `FORWARD_YAMAHA_REMOTE` and `FORWARD_DREAMBOX_REMOTE` at `1`: the Arduino passes on every key of the three remotes.
* **B. Away from the devices**, in the direction where the Sony remote is used. The Yamaha and Dreambox remotes reach their devices directly, so set `FORWARD_YAMAHA_REMOTE` and `FORWARD_DREAMBOX_REMOTE` to `0`.
  Keys with a rule are still converted; the Sony remote's other keys are still passed on to the TV.

* All IR output uses a **hardware carrier from Timer3** (pin 5, `OC3A`): 38 kHz for the Dreambox and the Yamaha amplifier, 40 kHz for re-sent Sony codes.

## Hardware

* Arduino Mega 2560
* KY-022 IR receiver module, signal on **pin 52**
* IR LED on **pin 5**, switched by a transistor (details and resistor values in [`docs/hardware.md`](docs/hardware.md))

## Quick start

1. Wire the hardware as described in [`docs/hardware.md`](docs/hardware.md).
2. Open `src/ir_converter_smooth_serial/ir_converter_smooth_serial.ino` in the Arduino IDE, select **Arduino Mega or Mega 2560** and upload.
3. The sketch ships with `USE_SERIAL 1`: it prints every code it receives from the three remotes (115200 baud), which is how the codes for new rules are learned. Set it to `0` for stand-alone use from a power bank.
4. To change or add keys, edit the `RULES` table in the sketch. Dreambox key codes are listed in [`docs/dreambox-key-codes.md`](docs/dreambox-key-codes.md).

The first line of the sketch contains its version and build date.

## Repository layout

```
src/ir_converter_smooth_serial/   the converter and repeater sketch (three remotes, version 2.0)
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

* **Version 2.0 (three remotes) has only been tested in a simulator**, not yet on the real devices. Versions up to 1.6 were tested on the TV, the Dreambox and the amplifier.
* Yamaha remote: how it behaves when a key is held (full frames or short repeat codes) has not been captured. The sketch accepts both; a held key is passed on only after the remote has shown that it is held, so a tap stays a single frame.
* The Arduino does not receive while it transmits: a key pressed during a transmission (about 40 ms to 340 ms) is not seen.

* Tested on a real Dreambox 500HD: **Menu, Up, Down, Right, OK**. Two more codes (digit 0 and volume up) appear in captures of the original remote. All other Dreambox codes come from the module's key table and are **not tested yet**.
* Yamaha NEC codes were learned from the Yamaha remote itself.
* Re-sending unmapped Sony codes: versions 1.2 to 1.4 did not make the TV react. Version 1.5 sends the press the way the frequency scan did (see [`tools/ir_sony_freq_scan`](tools/ir_sony_freq_scan)), and the author confirmed that it **works**. The IR LED must point at the TV's IR window.
* Yamaha volume while a key is held: with short NEC repeat codes (up to 1.3) the amplifier did not keep changing the volume; since 1.4 the full NEC frame is repeated about every 120 ms, and the author confirmed that this works.
* The key-frame check digit and flag nibble were derived from the module and from three captured frames of one key. They match all captured frames; other keys use the same formula.
* This project is not affiliated with Dreambox, Sony or Yamaha. The kernel module itself is **not** included, only numbers derived from it.
