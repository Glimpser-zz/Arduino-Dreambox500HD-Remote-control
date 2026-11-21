# Tools

* `dreambox_decode/` : receiver on pin 52. Prints every frame of an IR remote as an 8-digit hexadecimal word with the check result and the start time of each frame.
  Point the original Dreambox remote at the receiver; a tap shows a down pair, repeat pairs and an up pair.
* `ir_rawdump/` : receiver on pin 52. Prints the raw mark (`M`) and space (`S`) lengths in microseconds of any IR frame.

Both use the Serial Monitor at 115200 baud.
* `ir_sony_freq_scan/` : IR LED on pin 5. Type 1 to 7 in the Serial Monitor to send the Sony "Mute" command to the TV at 36 to 42 kHz. Used to find out which carrier the TV accepts (all of them, on the author's TV).
* `ir_sony_loopback_check/` : IR LED on pin 5 and receiver on pin 52, LED 2 to 3 cm from the receiver. Sends one Sony frame per carrier frequency and prints how the receiver sees the bursts, to compare with a real Sony remote.

Note: the two Sony tools (`ir_sony_freq_scan`, `ir_sony_loopback_check`) still use pin 5 (Timer3 output OC3A) for the IR LED. With the LCD shield fitted, pin 5 belongs to the shield: either remove the shield for these tests or change the pin and the timer output in the tool (pin 3 is `OC3C`).
