# Tools

* `dreambox_decode/` : receiver on pin 52. Prints every frame of an IR remote as an 8-digit hexadecimal word with the check result and the start time of each frame.
  Point the original Dreambox remote at the receiver; a tap shows a down pair, repeat pairs and an up pair.
* `ir_rawdump/` : receiver on pin 52. Prints the raw mark (`M`) and space (`S`) lengths in microseconds of any IR frame.

Both use the Serial Monitor at 115200 baud.
