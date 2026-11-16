# Sony remote to Yamaha / Dreambox mapping

The codes below were received with the converter's own receiver (the sketch prints them with `USE_SERIAL 1`).
Sony 12/15/20-bit SIRC codes: the value is the transmitted bits, least significant first; address and command are `value >> 7` and `value & 0x7F`.

| Sony key | Sony bits | Sony value | Address / command | Sends to | Sends | Continuous |
|---|---:|---|---|---|---|---|
| Volume up | 12 | `0x092` | 1 / 18 | Yamaha amplifier (NEC) | Yamaha Volume up (`0xE51A857A`) | yes |
| Volume down | 12 | `0x093` | 1 / 19 | Yamaha amplifier (NEC) | Yamaha Volume down (`0xE41B857A`) | yes |
| Audio | 12 | `0x097` | 1 / 23 | Yamaha amplifier (NEC) | Yamaha Stereo (`0x7788857A`) | no |
| Subtitle | 15 | `0x4BA8` | 151 / 40 | Yamaha amplifier (NEC) | Yamaha Movie (`0x748B857A`) | no |
| Help | 15 | `0x624D` | 196 / 77 | Yamaha amplifier (NEC) | Yamaha Night (`0x6A95857A`) | no |
| Mute | 12 | `0x094` | 1 / 20 | Yamaha amplifier (NEC) | Yamaha Mute (`0xE31C857A`) | no |
| DVR | 15 | `0x0D73` | 26 / 115 | Yamaha amplifier (NEC) | Yamaha Power on (`0xE21D857A`) | no |
| Guide | 15 | `0x525B` | 164 / 91 | Yamaha amplifier (NEC) | Yamaha Power off (`0xE11E857A`) | no |
| Jump | 12 | `0x0BB` | 1 / 59 | Dreambox | Dreambox Exit | no |
| Yellow | 15 | `0x4BA7` | 151 / 39 | Dreambox | Dreambox Volume down | yes |
| Blue | 15 | `0x4BA4` | 151 / 36 | Dreambox | Dreambox Down | no |
| Red | 15 | `0x4BA5` | 151 / 37 | Dreambox | Dreambox Up | no |
| Green | 15 | `0x4BA6` | 151 / 38 | Dreambox | Dreambox Volume up | yes |
| Netflix | 15 | `0x0D7C` | 26 / 124 | Dreambox | Dreambox OK | no |
| Google Play | 15 | `0x6246` | 196 / 70 | Dreambox | Dreambox Yellow | no |
| CH+ | 12 | `0x090` | 1 / 16 | Dreambox | Dreambox Right | no |
| CH- | 12 | `0x091` | 1 / 17 | Dreambox | Dreambox Left | no |
| 3D | 15 | `0x3BCD` | 119 / 77 | Dreambox | Dreambox Yellow | no |

Sony codes that are not in this table are re-sent to the TV unchanged (version 1.5: after the key is released, as one press of 3 frames, 45 ms apart, 40 kHz carrier).

## Yamaha NEC codes

The Yamaha amplifier uses the NEC protocol. The words were learned from the Yamaha remote. Bytes in transmission order: address, inverted address, command, inverted command
(the least significant byte of the word is sent first, each byte least significant bit first).

| Yamaha function | NEC word | Address | Inverted address | Command | Inverted command |
|---|---|---|---|---|---|
| Volume up | `0xE51A857A` | 0x7A | 0x85 | 0x1A | 0xE5 |
| Volume down | `0xE41B857A` | 0x7A | 0x85 | 0x1B | 0xE4 |
| Stereo | `0x7788857A` | 0x7A | 0x85 | 0x88 | 0x77 |
| Movie | `0x748B857A` | 0x7A | 0x85 | 0x8B | 0x74 |
| Night | `0x6A95857A` | 0x7A | 0x85 | 0x95 | 0x6A |
| Mute | `0xE31C857A` | 0x7A | 0x85 | 0x1C | 0xE3 |
| Power on | `0xE21D857A` | 0x7A | 0x85 | 0x1D | 0xE2 |
| Power off | `0xE11E857A` | 0x7A | 0x85 | 0x1E | 0xE1 |

NEC timing used by the sketch: leader 9 ms burst + 4.5 ms space, bits `562 us` burst + `562 us` (0) or `1688 us` (1) space, stop burst `562 us`;
while a key is held, version 1.4 sends the full frame again about every 120 ms. (Up to version 1.3 it sent short repeat codes, 9 ms burst + 2.25 ms space + 562 us burst, every 108 ms; the amplifier did not keep changing the volume with them. Set `NEC_HOLD_FULL_FRAME` to 0 to get them back.)

## Rules for the Yamaha and Dreambox remotes (version 2.0)

Every rule in the `RULES` table of the sketch has the same fields:

```
{ remote, bits, value, target, value, continuous, bits-for-Sony-target }
```

| Field | Sony remote | Yamaha remote | Dreambox remote |
|---|---|---|---|
| `remote` | `IN_SONY` | `IN_NEC` | `IN_DREAMBOX` |
| `bits` | 12, 15 or 20 | 32 | 0 |
| `value` | value printed by the sketch | NEC word printed by the sketch | Dreambox key code printed by the sketch |

| Target | `target` | value | last field |
|---|---|---|---|
| Dreambox | `OUT_DREAMBOX` | key code, for example `K_MENU` | not used |
| Yamaha amplifier | `OUT_NEC` | NEC word | not used |
| Sony TV | `OUT_SONY` | Sony value | 12, 15 or 20 (bits) |

With `USE_SERIAL 1` the sketch prints the code of every key it receives, for example:

```
SONY bits=12 value=0x94  -> rule found
YAMAHA remote  NEC word=0xE31C857A  -> no rule (passed on)
DREAMBOX remote  key=37 header=0xB3F441A  -> no rule (passed on)
```

Examples (they are in the sketch as comments):

```
{ IN_NEC,      32, 0xE31C857AUL, OUT_DREAMBOX, K_MENU,        false },        // Yamaha Mute   -> Dreambox Menu
{ IN_NEC,      32, 0x7788857AUL, OUT_SONY,     0x094,         false, 12 },    // Yamaha Stereo -> Sony TV Mute
{ IN_DREAMBOX, 0,  37,           OUT_NEC,      0xE51A857AUL,  false },        // Dreambox OK   -> Yamaha Volume up
```

`continuous = true` makes a key repeat while it is held (for Dreambox and Yamaha targets); a Sony target is always one press.
