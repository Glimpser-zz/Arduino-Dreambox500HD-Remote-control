// Raw IR dump - Arduino Mega 2560
// KY-022 receiver signal on pin 52. Serial Monitor at 115200 baud.
// Point any remote (for example the Yamaha amplifier remote) at the receiver and press a key.
// Every burst of signal is printed as mark (M) and space (S) lengths in microseconds.

const uint8_t RX_PIN = 52;
const uint16_t MAXRAW = 200;
const uint32_t END_US = 20000UL;        // 20 ms of silence ends a frame
uint16_t raw[MAXRAW];

void setup() {
  Serial.begin(115200);
  pinMode(RX_PIN, INPUT);
  Serial.println(F("Ready. Press a key on the remote."));
}

void loop() {
  if (digitalRead(RX_PIN) == HIGH) return;
  uint16_t n = 0;
  uint8_t level = LOW;
  bool ended = false;
  while (n != MAXRAW && !ended) {
    uint32_t s = micros();
    while (digitalRead(RX_PIN) == level) {
      if ((uint32_t)(micros() - s) > END_US) { ended = true; break; }
    }
    if (!ended) {
      uint32_t d = micros() - s;
      raw[n++] = d > 65535UL ? 65535 : (uint16_t)d;
      level = !level;
    }
  }
  Serial.print(F("Frame, durations: ")); Serial.println(n);
  for (uint16_t i = 0; i != n; i++) {
    Serial.print(i % 2 == 0 ? F("M") : F("S"));
    Serial.print(raw[i]);
    Serial.print(i % 8 == 7 ? '\n' : ' ');
  }
  Serial.println();
}
