#include <Arduino.h>
#include <avr/interrupt.h>
#include <avr/io.h>

// ir_sony_freq_scan | version 1.1 | build date 2026-11-06 | Sony TV carrier frequency selector, hardware timer (Timer3, pin 5)
//
// IR LED + driver on pin 5, built-in LED on pin 13. Serial Monitor at 115200 baud, line ending "Newline" or "No line ending".
//
// Type a number from 1 to 7 and the board sends the Sony "Mute" command (12 bits, address 1, command 20, value 0x094)
// to the TV with the chosen carrier frequency: 1 = 36 kHz ... 7 = 42 kHz. A press is 3 frames, 45 ms apart.
// Hold the IR LED 2-3 cm from the TV's IR window and watch the TV's IR indicator light and the mute symbol.
// Result on the author's TV (KD-49X8000H): the TV reacted at every frequency from 36 to 42 kHz.

const uint8_t TX_PIN = 5;              // OC3A / Timer3
const uint8_t STATUS_LED = 13;
const uint8_t FIRST_KHZ = 36;
const uint8_t LAST_KHZ = 42;
const uint8_t SONY_BITS = 12;
const uint32_t SONY_VALUE = 0x094;     // Mute: address 1, command 20 (the code the Sony remote sends)
const uint16_t SONY_FRAME_MS = 45;     // frame start to frame start
const uint8_t SONY_FRAMES = 3;         // a SIRC command is sent at least three times

// Same timer set-up as the tested sketch, for any carrier frequency in whole kHz.
void setCarrier(uint8_t khz) {
  uint8_t oldSREG = SREG;
  cli();

  // Stop and disconnect Timer3.
  TCCR3A = 0;
  TCCR3B = 0;

  // Fast PWM mode 14, TOP = ICR3, prescaler = 1.
  ICR3 = (uint16_t)(F_CPU / (khz * 1000UL)) - 1;
  OCR3A = (ICR3 + 1) / 3;
  TCNT3 = 0;

  // Start Timer3 with output disconnected.
  TCCR3A = _BV(WGM31);
  TCCR3B = _BV(WGM33) | _BV(WGM32) | _BV(CS30);

  SREG = oldSREG;

  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, LOW);
}

// One burst of carrier of the given length.
void burst(uint16_t us) {
  uint8_t oldSREG = SREG;
  cli();

  // Begin each burst at a known timer phase.
  TCNT3 = 0;

  // Connect OC3A in non-inverting PWM mode.
  TCCR3A |= _BV(COM3A1);

  SREG = oldSREG;

  delayMicroseconds(us);

  oldSREG = SREG;
  cli();

  // Disconnect OC3A at the end of the burst.
  TCCR3A &= ~(_BV(COM3A1) | _BV(COM3A0));

  SREG = oldSREG;

  // Hold pin 5 LOW between bursts.
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

// Sony SIRC: start 2400 us burst + 600 us space; bit 0 = 600 us burst, bit 1 = 1200 us burst, each + 600 us space;
// bits least significant first.
void sendSonyFrame(uint8_t bits, uint32_t value) {
  burst(2400);
  idle(600);
  for (uint8_t i = 0; i < bits; i++) {
    burst(((value >> i) & 1) ? 1200 : 600);
    idle(600);
  }
}

void sendSonyPress() {
  digitalWrite(STATUS_LED, HIGH);
  for (uint8_t f = 0; f < SONY_FRAMES; f++) {
    uint32_t t0 = millis();
    sendSonyFrame(SONY_BITS, SONY_VALUE);
    while ((uint32_t)(millis() - t0) < SONY_FRAME_MS) {}
  }
  digitalWrite(STATUS_LED, LOW);
}

void blinkSelection(uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    digitalWrite(STATUS_LED, HIGH);
    delay(300);
    digitalWrite(STATUS_LED, LOW);
    delay(300);
  }
}

void processSelection(char input) {
  if (input < '1' || input > '7') {
    Serial.println(F("Invalid input. Enter a number from 1 to 7."));
    return;
  }

  uint8_t selection = input - '0';
  uint8_t khz = FIRST_KHZ + selection - 1;

  setCarrier(khz);

  Serial.print(F("Selected frequency: "));
  Serial.print(khz);
  Serial.println(F(" kHz"));

  Serial.print(F("LED blinks: "));
  Serial.println(selection);

  blinkSelection(selection);

  delay(500);
  Serial.println(F("Sending Sony Mute..."));
  sendSonyPress();
  Serial.println(F("Transmission completed."));
  Serial.println(F("Enter next number (1-7):"));
}

void setup() {
  pinMode(STATUS_LED, OUTPUT);
  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, LOW);

  Serial.begin(115200);

  Serial.println();
  Serial.println(F("Sony IR frequency selector"));
  Serial.println(F("1 = 36 kHz"));
  Serial.println(F("2 = 37 kHz"));
  Serial.println(F("3 = 38 kHz"));
  Serial.println(F("4 = 39 kHz"));
  Serial.println(F("5 = 40 kHz"));
  Serial.println(F("6 = 41 kHz"));
  Serial.println(F("7 = 42 kHz"));
  Serial.println(F("Enter a number from 1 to 7:"));
}

void loop() {
  while (Serial.available() > 0) {
    char input = Serial.read();

    // Ignore CR/LF characters sent by the Serial Monitor.
    if (input == '\r' || input == '\n') {
      continue;
    }

    processSelection(input);
  }
}
