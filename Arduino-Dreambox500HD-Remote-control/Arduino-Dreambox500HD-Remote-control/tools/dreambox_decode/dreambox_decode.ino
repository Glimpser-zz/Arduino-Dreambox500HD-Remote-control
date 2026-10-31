// Dreambox remote decoder - Arduino Mega 2560
// KY-022 receiver on pin 52. Serial Monitor at 115200 baud.
// Point the ORIGINAL remote at the receiver: tap one key once, wait, then hold the same key for about 1 second.
// The sketch prints every frame, the time each frame starts, and the pause between frames.

const uint8_t RX_PIN = 52;
const uint16_t MAXMARKS = 220;
const uint32_t END_SILENCE_US = 500000UL;   // half a second of silence ends one capture

uint32_t t[MAXMARKS];                       // start time of every mark
uint16_t n;

bool waitLevel(uint8_t level, uint32_t timeoutUs) {
  uint32_t s = micros();
  while (digitalRead(RX_PIN) != level) {
    if ((uint32_t)(micros() - s) > timeoutUs) return false;
  }
  return true;
}

void printFrame(uint32_t w, uint32_t startMs) {
  uint8_t sum = 0;
  Serial.print(F("Frame 0x"));
  for (int8_t s = 28; s != -4; s -= 4) {
    uint8_t nb = (w >> s) & 0xF;
    sum += nb;
    Serial.print(nb, HEX);
  }
  Serial.print((sum & 0xF) == 0 ? F("  check OK") : F("  check FAIL"));
  Serial.print(F("  starts at ")); Serial.print(startMs); Serial.println(F(" ms"));
}

void decode() {
  uint8_t count = 0;
  uint32_t word = 0;
  Serial.println(F("----"));
  for (uint16_t i = 1; i != n; i++) {
    uint32_t dt = t[i] - t[i - 1];
    uint32_t units = dt * 27UL / 2UL;                    // timer units used by the module
    if (units > 12188UL && units < 41517UL) {
      if (count < 8) {
        word = (word << 4) | (((units - 12189UL) / 1833UL) & 0xF);
        count++;
        if (count == 8) printFrame(word, (t[i - 7] - t[0]) / 1000UL);
      }
    } else {
      if (count != 0 && count < 8) { Serial.print(F("Partial frame, nibbles: ")); Serial.println(count); }
      Serial.print(F("Pause before next frame: ")); Serial.print(dt); Serial.println(F(" us"));
      count = 0;
      word = 0;
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RX_PIN, INPUT);
  Serial.println(F("Ready. Press a key on the remote."));
}

void loop() {
  n = 0;
  while (digitalRead(RX_PIN) == HIGH) {}                // wait for the first mark
  t[n++] = micros();
  while (n != MAXMARKS) {
    if (!waitLevel(HIGH, 3000)) break;                  // end of this mark
    if (!waitLevel(LOW, END_SILENCE_US)) break;         // start of next mark, or end of capture
    t[n++] = micros();
  }
  decode();
  delay(300);
}
