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
| Yellow | 15 | `0x4BA7` | 151 / 39 | Dreambox | Dreambox Left | no |
| Blue | 15 | `0x4BA4` | 151 / 36 | Dreambox | Dreambox Down | no |
| Red | 15 | `0x4BA5` | 151 / 37 | Dreambox | Dreambox Up | no |
| Green | 15 | `0x4BA6` | 151 / 38 | Dreambox | Dreambox Right | no |
| Netflix | 15 | `0x0D7C` | 26 / 124 | Dreambox | Dreambox OK | no |
| Google Play | 15 | `0x6246` | 196 / 70 | Dreambox | Dreambox Yellow | no |
| CH+ | 12 | `0x090` | 1 / 16 | Dreambox | Dreambox Volume up | yes |
| CH- | 12 | `0x091` | 1 / 17 | Dreambox | Dreambox Volume down | yes |
| 3D | 15 | `0x3BCD` | 119 / 77 | Dreambox | Dreambox Yellow | no |

Sony codes that are not in this table are re-sent to the TV unchanged.

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
while a key is held, short repeat codes (9 ms burst, 2.25 ms space, 562 us burst) every 108 ms.
