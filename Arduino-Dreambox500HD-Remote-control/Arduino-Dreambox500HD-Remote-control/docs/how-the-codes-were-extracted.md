# How the Dreambox codes were extracted

Source: the kernel modules `dreambox_rc2.ko` and `dreambox_keyboard.ko` from a Dreambox 500HD (Enigma2) image. They are 32-bit little-endian **MIPS** ELF relocatable modules.
The modules are **not** part of this repository; only the numbers derived from them are.

## 1. Locate the data

```
readelf -S -W dreambox_rc2.ko
```

In the analysed build: `.rodata` is 0x440 bytes and `.data` is 0x2C0 bytes. Offsets below are relative to the start of each section.

| What | Where | Format |
|---|---|---|
| remote IDs of decoder B | `.rodata + 0x08` | 4 bytes: 0x32, 0x34, 0x33, 0x35 |
| header frames of decoder A | `.rodata + 0x0C` | 12 little-endian 32-bit words (index 4 to 6 are empty) |
| key table of decoder B, IDs 0x34/0x35 | `.rodata + 0x3C` | 256 little-endian 16-bit Linux key codes |
| key table of decoder B, IDs 0x32/0x33 | `.rodata + 0x23C` | 256 little-endian 16-bit Linux key codes |
| header masks | `.data + 0x270` | three 32-bit words: 0x0F, 0xF00, 0x0F |
| **key table of decoder A** | `.data + 0x70` | 256 little-endian 16-bit Linux key codes (index = key code) |

## 2. Dump the tables

```python
import struct, subprocess, re
f = "dreambox_rc2.ko"
data = open(f, "rb").read()
hdr = subprocess.run(["readelf", "-S", "-W", f], capture_output=True, text=True).stdout
def section(name):
    for line in hdr.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)", line)
        if m and m.group(1) == name:
            off, size = int(m.group(2), 16), int(m.group(3), 16)
            return data[off:off + size]
rodata, sdata = section(".rodata"), section(".data")
keymap_a = struct.unpack("<256H", sdata[0x70:0x70 + 512])
print({i: hex(v) for i, v in enumerate(keymap_a) if v})
```

## 3. Understand the decoders

The code of the module was read with a small MIPS disassembler (relocations resolved by hand). The relevant findings:

* the interval decoder converts each interval to a nibble and checks the nibble sum;
* the header table arms the decoder for one frame;
* key-frame bits 8 to 15 index the key table; bit 23 is the repeat flag and bit 20 together with bit 23 is the key-up flag;
* the module prints `RCU: dropping invalid key code %08x` for frames that fail its checks.

## 4. Check against a real remote

A real Dreambox remote was captured with the Arduino (`tools/dreambox_decode`). Header `0x0B3F441A` and the key frames `0x0D030000`, `0x05830000`, `0x04930000`
(key 0) and `0x03030A00` (key 10) were decoded from the captures and match the formulas in [dreambox-ir-protocol.md](dreambox-ir-protocol.md).
The scale factor from microseconds to the module's timer units (13.5 units per microsecond) was found by matching this capture.

## 5. Check against the box

The generated frames were transmitted by the Arduino; Menu, Up, Down, Right and OK worked.
