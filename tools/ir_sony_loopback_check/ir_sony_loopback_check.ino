#include <Arduino.h>
#include <avr/interrupt.h>
#include <avr/io.h>

// ir_sony_loopback_check | version 1.0 | build date 2026-11-06 | compares our Sony signal with the real remote, as seen by the same receiver
//
// KY-022 receiver on pin 52, IR LED + driver on pin 5 (Timer3 hardware carrier), Serial Monitor at 115200 baud.
// Hold the IR LED 2-3 cm from the KY-022 receiver, pointing at it.
//
// For each carrier frequency from 36 to 42 kHz the board sends ONE Sony frame (value 0x094, 12 bits: the "Mute" key)
// and records, with pin-change interrupts, how the receiver sees it: the length of the start burst and of the bit bursts.
// The numbers are printed so that they can be compared with the real Sony remote, as seen by the same receiver:
//
//     real remote (desk test):  start = 2404   bit bursts min = 568 to 584   max = 1196   threshold = 882 to 890
//
// If our start burst and bit bursts are close to those numbers, the envelope of our signal is the same as the remote's.
// The receiver only gives the envelope (it removes the carrier), so it can NOT tell the carrier frequency of the real remote.

const uint8_t RX_PIN = 52;             // PB1 = PCINT1
const uint8_t TX_PIN = 5;              // OC3A / Timer3
const uint8_t STATUS_LED = 13;
const uint8_t FIRST_KHZ = 36;
const uint8_t LAST_KHZ = 42;
const uint8_t SONY_BITS = 12;
const uint32_t SONY_VALUE = 0x094;     // Mute: address 1, command 20

// ---------------- edges seen by the receiver ----------------
const uint8_t MAX_EDGES = 64;
volatile uint32_t edgeTime[MAX_EDGES];
volatile uint8_t edgeLevel[MAX_EDGES];
volatile uint8_t edgeCount = 0;

ISR(PCINT0_vect) {
  uint8_t level = (PINB & _BV(PB1)) ? 1 : 0;
  uint8_t n = edgeCount;
  if (n < MAX_EDGES) {
    edgeTime[n] = micros();
    edgeLevel[n] = level;
    edgeCount = n + 1;
  }
}

// Same timer set-up as the tested sketch, for any carrier frequency in whole kHz.
void setCarrier(uint8_t khz) {
  uint8_t oldSREG = SREG;
  cli();
  TCCR3A = 0;
  TCCR3B = 0;
  ICR3 = (uint16_t)(F_CPU / (khz * 1000UL)) - 1;
  OCR3A = (ICR3 + 1) / 3;
  TCNT3 = 0;
  TCCR3A = _BV(WGM31);
  TCCR3B = _BV(WGM33) | _BV(WGM32) | _BV(CS30);
  SREG = oldSREG;
  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, LOW);
}

void burst(uint16_t us) {
  uint8_t oldSREG = SREG;
  cli();
  TCNT3 = 0;
  TCCR3A |= _BV(COM3A1);
  SREG = oldSREG;
  delayMicroseconds(us);
  oldSREG = SREG;
  cli();
  TCCR3A &= ~(_BV(COM3A1) | _BV(COM3A0));
  SREG = oldSREG;
  digitalWrite(TX_PIN, LOW);
}

void idle(uint32_t us) {
  while (us >= 16000UL) {
    delayMicroseconds(16000);
    us -= 16000UL;
  }
  if (us > 0) {
    delayMicroseconds((uint16_t)us);
  }
}

// Sony SIRC: start 2400 us burst + 600 us space; bit 0 = 600 us burst, bit 1 = 1200 us burst, each + 600 us space.
void sendSonyFrame(uint8_t bits, uint32_t value) {
  burst(2400);
  idle(600);
  for (uint8_t i = 0; i < bits; i++) {
    burst(((value >> i) & 1) ? 1200 : 600);
    idle(600);
  }
}

// Turn the burst lengths into start length, min/max of the bit bursts, threshold and value (same rule as the converter).
bool decodeMarks(const uint16_t *marks, uint8_t count, uint16_t *start, uint16_t *mn, uint16_t *mx, uint16_t *thr, uint32_t *value) {
  if (count != SONY_BITS + 1) return false;
  *start = marks[0];
  uint16_t lo = 65535, hi = 0;
  for (uint8_t i = 1; i < count; i++) {
    if (marks[i] < lo) lo = marks[i];
    if (marks[i] > hi) hi = marks[i];
  }
  uint16_t t = (hi > lo + lo / 2) ? (lo + hi) / 2 : 900;
  uint32_t v = 0;
  for (uint8_t i = 1; i < count; i++) {
    if (marks[i] > t) v |= 1UL << (i - 1);
  }
  *mn = lo; *mx = hi; *thr = t; *value = v;
  return true;
}

void analyse(uint8_t khz) {
  uint8_t n = edgeCount;
  uint16_t marks[20];
  uint8_t m = 0;
  bool inMark = false;
  uint32_t fallAt = 0;
  for (uint8_t i = 0; i < n; i++) {
    uint8_t level = edgeLevel[i];
    uint32_t t = edgeTime[i];
    if (level == 0) {                      // output goes LOW: a burst starts
      inMark = true;
      fallAt = t;
    } else if (inMark) {                   // output goes HIGH: the burst ends
      if (m < 20) marks[m++] = (uint16_t)(t - fallAt);
      inMark = false;
    }
  }
  Serial.print(F("carrier "));
  Serial.print(khz);
  Serial.print(F(" kHz: edges "));
  Serial.print(n);
  Serial.print(F(", bursts "));
  Serial.print(m);
  Serial.print(F(" -> "));
  uint16_t start, mn, mx, thr;
  uint32_t value;
  if (decodeMarks(marks, m, &start, &mn, &mx, &thr, &value)) {
    Serial.print(F("start="));
    Serial.print(start);
    Serial.print(F("  bit bursts min="));
    Serial.print(mn);
    Serial.print(F(" max="));
    Serial.print(mx);
    Serial.print(F("  threshold="));
    Serial.print(thr);
    Serial.print(F("  value=0x"));
    Serial.print(value, HEX);
    Serial.println(value == SONY_VALUE ? F("  (correct)") : F("  (WRONG)"));
  } else {
    Serial.print(F("not a complete frame, burst lengths:"));
    for (uint8_t i = 0; i < m; i++) {
      Serial.print(' ');
      Serial.print(marks[i]);
    }
    Serial.println();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED, OUTPUT);
  pinMode(RX_PIN, INPUT);
  PCICR |= _BV(PCIE0);                     // pin-change interrupt group 0 (pins 50-53, 10-13)
  PCMSK0 |= _BV(PCINT1);                   // only pin 52
  Serial.println(F("Sony loopback check. Real remote seen by this receiver: start=2404, bit bursts min=568-584 max=1196"));
  delay(3000);
}

void loop() {
  for (uint8_t khz = FIRST_KHZ; khz <= LAST_KHZ; khz++) {
    setCarrier(khz);
    delay(200);
    edgeCount = 0;
    digitalWrite(STATUS_LED, HIGH);
    sendSonyFrame(SONY_BITS, SONY_VALUE);
    digitalWrite(STATUS_LED, LOW);
    delay(30);                             // let the last edges arrive
    analyse(khz);
    delay(1000);
  }
  Serial.println(F("----"));
  delay(5000);
}
