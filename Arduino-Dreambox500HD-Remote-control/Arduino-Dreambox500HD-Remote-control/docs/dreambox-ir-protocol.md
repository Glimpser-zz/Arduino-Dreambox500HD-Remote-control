# Dreambox 500HD IR protocol (type A, "interval" remote)

Everything in this document was read from the kernel module `dreambox_rc2.ko` or measured from a captured remote.
Statements marked *(inferred)* were derived by matching captured data and are not read from the module.

The module contains two decoders. The remote that was captured uses decoder **A** (this page). Decoder **B** is described at the end for reference.

## Symbols: one frame = 9 bursts, 8 nibbles

* A frame is **9 short carrier bursts** (marks, about 200 us each; 140 to 224 us measured at a KY-022 output).
* The **8 intervals between consecutive bursts** (start of one burst to start of the next) carry 8 nibbles. The first interval is the **most significant** nibble.
* Measured: nibble `n` (0 to 15) gives an interval of about `965 + 136 * n` microseconds.
* The module converts each interval with `units = interval_us * 13.5` *(inferred)*, accepts `12188 < units < 41517` (about 903 to 3075 us) and computes
  `nibble = ((units - 12189) / 1833) & 0xF`. An interval outside this range **resets** the frame counter, so frames must be separated by a pause longer than 3.1 ms.
* A frame is valid when the **sum of its 8 nibbles is a multiple of 16**.

## Pairs: header frame + key frame

A key is sent as a **pair**:

1. a **header frame** (below), then
2. a pause of about **13.17 ms** (burst start to burst start between the last burst of the header and the first burst of the key frame),
3. the **key frame**.

The header frame arms the decoder for exactly one following frame *(read from the module)*. The captured remote uses the header `0x0B3F441A`.

### Header frames in the module table

Enabled by default: indices 0 to 3 (mask 0x0F) and 8 to 11 (mask 0xF00). Only `0x0B3F441A` was seen on a real remote.

| Table index | Header frame |
|---:|---|
| 0 | `0x0E0F441A` |
| 1 | `0x0D1F441A` |
| 2 | `0x0C2F441A` |
| 3 | `0x0B3F441A` |
| 7 | `0x2B1F441A` |
| 8 | `0x3B0F441A` |
| 9 | `0x3A1F441A` |
| 10 | `0x392F441A` |
| 11 | `0x383F441A` |

## Key frame layout

```
bit 31..28  0
bit 27..24  check digit
bit 23..20  flags: 0 = key down, 8 = repeat, 9 = key up
bit 19..16  3
bit 15..8   key code (0..127)
bit  7..0   0
```

* `check = (16 - ((3 + flags + (key >> 4) + (key & 15)) & 15)) & 15`, so that the nibble sum is a multiple of 16.
* The module also requires `frame & 0xC06C80FF == 0` for key frames.
* The key code is looked up in a 256-entry table of Linux input codes. See [key codes](dreambox-key-codes.md).

Frames captured from the original remote, and what the formula gives:

| Key code | Frame | Captured | Formula |
|---|---|---|---|
| 0 | down | `0x0D030000` | `0x0D030000` |
| 0 | repeat | `0x05830000` | `0x05830000` |
| 0 | up | `0x04930000` | `0x04930000` |
| 10 | down | `0x03030A00` | `0x03030A00` |

## Timing of a key press

Measured from the original remote:

* pairs repeat every **about 118 ms** while the key is held (start of one header to start of the next); the pause before the next header is about 80.4 ms;
* the **up pair** follows the last pair after about 13.26 ms;
* the carrier is about 38 kHz.

A tap of the original remote produced: down pair, two repeat pairs, up pair. The sketch sends exactly this for single keys (tested on the box), and
down, repeat pairs every 118 ms while held, up at the release for continuous keys.

## Decoder B (not used by the captured remote)

A second decoder in the same module reads a 32-bit word `[b3 b2 b1 b0]`:

* `b3` = remote ID, one of `0x32`, `0x34`, `0x33`, `0x35` (enabled by a mask);
* `b1` = key code in bits 0 to 5, `0x40` and `0x80` are flags *(inferred from the code: repeat / release)*;
* `b0 = b1 ^ b2 ^ b3` (`b2` is free).

The key tables of this decoder differ for IDs 0x32/0x33 and IDs 0x34/0x35. They are not used by this project and have **not been tested**.

| 6-bit key code | IDs 0x32 / 0x33 | IDs 0x34 / 0x35 |
|---:|---|---|
| 0 | KEY_POWER | KEY_POWER |
| 1 | KEY_1 |  |
| 2 | KEY_2 |  |
| 3 | KEY_3 |  |
| 4 | KEY_4 | KEY_1 |
| 5 | KEY_5 | KEY_2 |
| 6 | KEY_6 | KEY_3 |
| 7 | KEY_7 | KEY_MUTE |
| 8 | KEY_8 | KEY_4 |
| 9 | KEY_9 | KEY_5 |
| 10 | KEY_PREVIOUS | KEY_6 |
| 11 | KEY_0 | KEY_PREVIOUS |
| 12 | KEY_NEXT | KEY_7 |
| 13 |  | KEY_8 |
| 14 | KEY_AUDIO | KEY_9 |
| 15 |  | KEY_NEXT |
| 16 | KEY_INFO | KEY_0 |
| 17 | KEY_UP | KEY_UP |
| 18 | KEY_MENU | KEY_EXIT |
| 19 | KEY_LEFT | KEY_VOLUMEUP |
| 20 | KEY_OK | KEY_LEFT |
| 21 | KEY_RIGHT | KEY_OK |
| 22 | KEY_HELP | KEY_RIGHT |
| 23 | KEY_DOWN | KEY_CHANNELUP |
| 24 | KEY_VIDEO | KEY_VOLUMEDOWN |
| 25 | KEY_VOLUMEUP | KEY_DOWN |
| 26 | KEY_MUTE | KEY_CHANNELDOWN |
| 27 | KEY_CHANNELUP | KEY_INFO |
| 28 | KEY_VOLUMEDOWN | KEY_MENU |
| 29 | KEY_EXIT |  |
| 30 | KEY_CHANNELDOWN |  |
| 31 | KEY_RED | KEY_RED |
| 32 | KEY_GREEN | KEY_GREEN |
| 33 | KEY_YELLOW | KEY_YELLOW |
| 34 | KEY_BLUE | KEY_BLUE |
| 35 | KEY_PREVIOUSSONG | KEY_TV |
| 36 | KEY_PLAY | KEY_RADIO |
| 37 | KEY_STOP | KEY_TEXT |
| 38 | KEY_NEXTSONG | KEY_HELP |
| 39 | KEY_TV |  |
| 40 | KEY_RADIO |  |
| 41 | KEY_TEXT |  |
| 42 | KEY_RECORD |  |
| 48 |  | KEY_AUDIO |
| 49 |  | KEY_VIDEO |
| 50 |  | KEY_PREVIOUSSONG |
| 51 |  | KEY_PLAY |
| 52 |  | KEY_STOP |
| 53 |  | KEY_NEXTSONG |
| 54 |  | KEY_RECORD |

The second module, `dreambox_keyboard.ko` (IR keyboard and mouse), was not analysed.
