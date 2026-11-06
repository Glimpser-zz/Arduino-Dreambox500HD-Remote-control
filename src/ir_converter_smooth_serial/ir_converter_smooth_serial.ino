// ir_converter_smooth_serial | version 1.3 | build date 2026-10-30 | carrier: hardware timer (Timer3, pin 5), 38 kHz for everything
#include <Arduino.h>
#include <avr/interrupt.h>
#include <avr/io.h>

// IR converter with natural key holding - Arduino Mega 2560, hardware 38 kHz carrier from Timer3
// Sony TV remote (KY-022 receiver on pin 52)  ->  Yamaha amplifier (NEC) / Dreambox (IR LED + resistor on pin 5).
//
// How keys behave, like the original remotes:
//   volume up / volume down (Yamaha and Dreambox) are continuous: the code is sent when the key goes down and repeated while the key stays down.
//   rules with repeat = true  : the code is sent when the key goes down and repeated while the key stays down.
//                               Dreambox: down pair, repeat pairs every 118 ms, "key up" pair at the release.
//                               Yamaha:   full NEC frame, then short NEC repeat codes every 108 ms.
//   rules with repeat = false : one single action, sent once the key has been released.
//   Sony codes without a rule are re-sent unchanged, as received, with the same 38 kHz hardware carrier.
// The built-in LED (pin 13) is on while something is being transmitted.

#define USE_SERIAL   1     // 1 = print received Sony codes on the Serial Monitor (115200 baud)
#define DEBUG_LED    0     // 1 = blink the built-in LED for a received key: 2 slow = rule found, 3 fast = no rule

#if USE_SERIAL
#define LOG(text) do { Serial.println(F(text)); Serial.flush(); } while (0)
#else
#define LOG(text) do { } while (0)
#endif

const uint8_t RX_PIN = 52;
const uint8_t TX_PIN = 5;              // OC3A / Timer3
const uint8_t STATUS_LED = 13;
const uint16_t CARRIER_KHZ = 38;             // Dreambox and Yamaha
const uint8_t  SONY_CARRIER_KHZ = 38;          // carrier for re-sent Sony codes (38 = same as everything else; 40 is the Sony standard)

// ---- Dreambox protocol (measured on the real remote) ----
const uint16_t MARK_US = 200;
const uint16_t GAP_BASE_US = 965;
const uint16_t GAP_STEP_US = 136;
const uint32_t HEADER_FRAME = 0x0B3F441AUL;
const uint32_t PAIR_GAP_US = 13168;
const uint32_t REPEAT_GAP_US = 80424;
const uint32_t RELEASE_GAP_US = 13260;
const uint16_t DREAMBOX_REPEAT_MS = 118;    // start of one pair to the start of the next
const uint8_t  DREAMBOX_MIN_REPEATS = 2;    // every press sends at least this many repeat pairs (tested)
const uint8_t FLAG_DOWN = 0x0, FLAG_REPEAT = 0x8, FLAG_UP = 0x9;

// ---- NEC (Yamaha) ----
const uint16_t NEC_REPEAT_MS = 108;         // start of the frame / of a repeat code to the start of the next
const uint8_t  NEC_TAP_REPEATS = 0;         // repeat codes after the frame for single (non-repeat) rules

// ---- Sony remote: no frame for this long = key released ----
const uint16_t RELEASE_SINGLE_MS = 120;     // keys with repeat = false (nothing is sent in the meantime)
const uint16_t RELEASE_HELD_MS = 200;       // Dreambox keys with repeat = true (our own sending makes us miss frames)
const uint16_t RELEASE_NEC_HELD_MS = 160;   // Yamaha keys with repeat = true

// ---- Dreambox key codes ----
const uint8_t K_MENU = 32, K_EXIT = 82, K_OK = 37, K_LEFT = 35, K_RIGHT = 36;
const uint8_t K_VOL_UP = 10, K_VOL_DOWN = 11, K_CH_UP = 13, K_CH_DOWN = 14;
const uint8_t K_YELLOW = 66;
const uint8_t K_UP = 33, K_DOWN = 34;

// ---------------- the rules ----------------
enum Out : uint8_t { OUT_DREAMBOX, OUT_NEC };

struct Rule {
  uint8_t  sonyBits;
  uint32_t sonyValue;
  Out      out;
  uint32_t outValue;
  bool     repeat;
};

const Rule RULES[] = {
  // Sony bits, Sony value, output,       output value,  continuous?
  // ---- Sony -> Yamaha amplifier (NEC) ----
  { 12, 0x092,  OUT_NEC,      0xE51A857AUL,  true  },   // Sony Volume up   -> Yamaha Volume up   (continuous)
  { 12, 0x093,  OUT_NEC,      0xE41B857AUL,  true  },   // Sony Volume down -> Yamaha Volume down (continuous)
  { 12, 0x097,  OUT_NEC,      0x7788857AUL,  false },   // Sony Audio       -> Yamaha Stereo
  { 15, 0x4BA8, OUT_NEC,      0x748B857AUL,  false },   // Sony Subtitle    -> Yamaha Movie
  { 15, 0x624D, OUT_NEC,      0x6A95857AUL,  false },   // Sony Help        -> Yamaha Night
  { 12, 0x094,  OUT_NEC,      0xE31C857AUL,  false },   // Sony Mute        -> Yamaha Mute
  { 15, 0x0D73, OUT_NEC,      0xE21D857AUL,  false },   // Sony DVR         -> Yamaha Power on
  { 15, 0x525B, OUT_NEC,      0xE11E857AUL,  false },   // Sony Guide       -> Yamaha Power off
  // ---- Sony -> Dreambox ----
  { 12, 0x0BB,  OUT_DREAMBOX, K_EXIT,        false },   // Sony Jump        -> Dreambox Exit
  { 15, 0x4BA7, OUT_DREAMBOX, K_LEFT,        false },   // Sony Yellow      -> Dreambox Left
  { 15, 0x4BA4, OUT_DREAMBOX, K_DOWN,        false },   // Sony Blue        -> Dreambox Down
  { 15, 0x4BA5, OUT_DREAMBOX, K_UP,          false },   // Sony Red         -> Dreambox Up
  { 15, 0x4BA6, OUT_DREAMBOX, K_RIGHT,       false },   // Sony Green       -> Dreambox Right
  { 15, 0x0D7C, OUT_DREAMBOX, K_OK,          false },   // Sony Netflix     -> Dreambox OK
  { 15, 0x6246, OUT_DREAMBOX, K_YELLOW,      false },   // Sony Google Play -> Dreambox Yellow
  { 12, 0x090,  OUT_DREAMBOX, K_VOL_UP,      true  },   // Sony CH+         -> Dreambox Volume up   (continuous)
  { 12, 0x091,  OUT_DREAMBOX, K_VOL_DOWN,    true  },   // Sony CH-         -> Dreambox Volume down (continuous)
  { 15, 0x3BCD, OUT_DREAMBOX, K_YELLOW,      false },   // Sony 3D          -> Dreambox Yellow
};
const uint8_t RULE_COUNT = sizeof(RULES) / sizeof(RULES[0]);

// --------------------------------------------------
// Timer3 hardware PWM on pin 5 (OC3A)
// --------------------------------------------------

uint8_t currentCarrierKhz = 0;

// Same timer set-up as the tested sketch, for any carrier frequency in whole kHz.
void setCarrier(uint8_t khz) {
  if (khz == currentCarrierKhz) return;

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
  currentCarrierKhz = khz;
}

void hardwareOn() {
  currentCarrierKhz = 0;
  setCarrier(CARRIER_KHZ);
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

// ---------------- Dreambox ----------------
void sendDreamboxFrame(uint32_t word) {
  burst(MARK_US);
  for (int8_t i = 7; i >= 0; i--) {
    uint8_t n = (word >> (4 * i)) & 0x0F;
    idle(GAP_BASE_US + (uint32_t)GAP_STEP_US * n - MARK_US);
    burst(MARK_US);
  }
}

uint32_t dreamboxKeyFrame(uint8_t key, uint8_t flags) {
  key &= 0x7F;
  uint8_t sum = 3 + flags + (key >> 4) + (key & 0x0F);
  uint8_t chk = (16 - (sum & 0x0F)) & 0x0F;
  return ((uint32_t)chk << 24) | ((uint32_t)flags << 20) | (0x03UL << 16) | ((uint32_t)key << 8);
}

// One pair = header frame + key frame. flags: FLAG_DOWN, FLAG_REPEAT or FLAG_UP.
void dreamboxPair(uint8_t key, uint8_t flags) {
  digitalWrite(STATUS_LED, HIGH);
  sendDreamboxFrame(HEADER_FRAME);
  idle(PAIR_GAP_US - MARK_US);
  sendDreamboxFrame(dreamboxKeyFrame(key, flags));
  digitalWrite(STATUS_LED, LOW);
}

// A complete single press: down, repeats, up (this exact sequence was tested on the box).
void dreamboxSequence(uint8_t key) {
  dreamboxPair(key, FLAG_DOWN);
  for (uint8_t i = 0; i < DREAMBOX_MIN_REPEATS; i++) {
    idle(REPEAT_GAP_US - MARK_US);
    dreamboxPair(key, FLAG_REPEAT);
  }
  idle(RELEASE_GAP_US - MARK_US);
  dreamboxPair(key, FLAG_UP);
}

// ---------------- Sony SIRC (pass-through of unmapped codes) ----------------
// Start: 2400 us burst + 600 us space. Bit 0: 600 us burst, bit 1: 1200 us burst, each followed by 600 us space.
// Bits are sent least significant first, exactly as they were received.
void sendSonyFrame(uint8_t bits, uint32_t value) {
  digitalWrite(STATUS_LED, HIGH);
  burst(2400);
  idle(600);
  for (uint8_t i = 0; i < bits; i++) {
    burst(((value >> i) & 1) ? 1200 : 600);
    idle(600);
  }
  digitalWrite(STATUS_LED, LOW);
}

void sendSonyRawFrame(const uint16_t *timings, uint8_t count) {
  digitalWrite(STATUS_LED, HIGH);
  for (uint8_t i = 0; i < count; i++) {
    if ((i & 1) == 0) burst(timings[i]); // receiver LOW = IR mark
    else idle(timings[i]);               // receiver HIGH = space
  }
  digitalWrite(STATUS_LED, LOW);
}

// ---------------- NEC ----------------
// Full frame: 9 ms mark, 4.5 ms space, 32 bits (first bit = bit 0 of the word), stop mark.
void sendNecFrame(uint32_t w) {
  digitalWrite(STATUS_LED, HIGH);
  burst(9000);
  idle(4500);
  for (uint8_t i = 0; i < 32; i++) {
    burst(562);
    idle(((w >> i) & 1) ? 1688 : 562);
  }
  burst(562);
  digitalWrite(STATUS_LED, LOW);
}

// Short repeat code: 9 ms mark, 2.25 ms space, 562 us mark.
void sendNecRepeat() {
  digitalWrite(STATUS_LED, HIGH);
  burst(9000);
  idle(2250);
  burst(562);
  digitalWrite(STATUS_LED, LOW);
}

void sendNecTap(uint32_t w) {
  uint32_t t0 = millis();
  sendNecFrame(w);
  for (uint8_t r = 0; r < NEC_TAP_REPEATS; r++) {
    while ((uint32_t)(millis() - t0) < (uint32_t)NEC_REPEAT_MS * (r + 1)) {}
    sendNecRepeat();
  }
}

#if DEBUG_LED
void blink(uint8_t times, uint16_t ms) {
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(STATUS_LED, HIGH);
    delay(ms);
    digitalWrite(STATUS_LED, LOW);
    delay(ms);
  }
}
#endif

// ---------------- receiving ----------------
const uint16_t MAXRAW = 80;
const uint32_t END_SPACE_US = 7000UL;
uint16_t raw[MAXRAW];
uint8_t rawLen;

bool captureFrame() {
  rawLen = 0;
  uint8_t level = LOW;
  while (rawLen != MAXRAW) {
    uint32_t limit = (level == LOW) ? 20000UL : END_SPACE_US;
    uint32_t start = micros();
    while (digitalRead(RX_PIN) == level) {
      if ((uint32_t)(micros() - start) > limit) return (level == HIGH) && (rawLen > 2);
    }
    uint32_t d = micros() - start;
    raw[rawLen++] = d > 65535UL ? 65535 : (uint16_t)d;
    level = !level;
  }
  return true;
}

// ---------------- key state: one Sony key at a time ----------------
int8_t activeRule = -1;        // rule of the Sony key that is down right now, -1 = none
uint32_t lastFrameAt = 0;      // time of the last Sony frame of that key
uint32_t lastSendAt = 0;       // start time of the last code sent for that key
uint8_t sentRepeats = 0;       // Dreambox repeat pairs sent for that key

// Unmapped Sony keys are repeated locally while held. Transmitting blocks the
// receiver, so relying only on incoming SIRC repeat frames can miss hold repeats.
bool activeUnknown = false;
uint8_t unknownBits = 0;
uint32_t unknownValue = 0;
uint32_t unknownLastFrameAt = 0;
uint32_t unknownLastSendAt = 0;
uint16_t unknownRaw[MAXRAW];
uint8_t unknownRawLen = 0;
const uint16_t SONY_REPEAT_MS = 45;

// Forward the captured mark/space sequence, not a newly synthesized SIRC frame.
void forwardUnknownSonyRaw() {
  LOG("  forward: unmapped Sony raw frame");
  setCarrier(SONY_CARRIER_KHZ);
  sendSonyRawFrame(unknownRaw, unknownRawLen);
  setCarrier(CARRIER_KHZ);
}

void runSingle(const Rule &r) {
  LOG("  send: single action");
  if (r.out == OUT_DREAMBOX) dreamboxSequence((uint8_t)r.outValue);
  else sendNecTap(r.outValue);
}

// Key goes down on a rule with repeat = true: send the first code at once.
void startHeld(const Rule &r) {
  LOG("  send: key down (held key)");
  uint32_t t0 = millis();
  if (r.out == OUT_DREAMBOX) dreamboxPair((uint8_t)r.outValue, FLAG_DOWN);
  else sendNecFrame(r.outValue);
  lastSendAt = t0;
  sentRepeats = 0;
}

// Complete whatever the active key still owes the devices, then forget it.
void finishActive() {
  if (activeRule < 0) return;
  const Rule &r = RULES[activeRule];
  if (!r.repeat) {
    runSingle(r);                                   // single action that was waiting for the key release
  } else if (r.out == OUT_DREAMBOX) {
    while (sentRepeats < DREAMBOX_MIN_REPEATS) {
      while ((uint32_t)(millis() - lastSendAt) < DREAMBOX_REPEAT_MS) {}
      uint32_t t0 = millis();
      dreamboxPair((uint8_t)r.outValue, FLAG_REPEAT);
      sentRepeats++;
      lastSendAt = t0;
    }
    dreamboxPair((uint8_t)r.outValue, FLAG_UP);
  }
  activeRule = -1;
}

// Called on every pass of loop(): repeats while held, completes the press at the release.
void serviceActive() {
  if (activeRule < 0) {
    if (!activeUnknown) return;
    uint32_t now = millis();
    if ((uint32_t)(now - unknownLastFrameAt) > RELEASE_SINGLE_MS) {
      activeUnknown = false;
      LOG("  unmapped Sony key released");
      return;
    }
    if ((uint32_t)(now - unknownLastSendAt) >= SONY_REPEAT_MS) {
      unknownLastSendAt = now;
      forwardUnknownSonyRaw();
    }
    return;
  }
  const Rule &r = RULES[activeRule];
  uint32_t now = millis();
  uint32_t quiet = (uint32_t)(now - lastFrameAt);

  if (!r.repeat) {
    if (quiet > RELEASE_SINGLE_MS) {                // key released: do the single action now
      runSingle(r);
      activeRule = -1;
    }
    return;
  }

  bool released = quiet > (r.out == OUT_DREAMBOX ? RELEASE_HELD_MS : RELEASE_NEC_HELD_MS);
  if (r.out == OUT_DREAMBOX) {
    if (!released || sentRepeats < DREAMBOX_MIN_REPEATS) {
      if ((uint32_t)(now - lastSendAt) >= DREAMBOX_REPEAT_MS) {
        LOG("  send: Dreambox repeat");
        dreamboxPair((uint8_t)r.outValue, FLAG_REPEAT);
        if (sentRepeats < 255) sentRepeats++;
        lastSendAt = now;
      }
    } else {
      LOG("  send: Dreambox key up (released)");
      dreamboxPair((uint8_t)r.outValue, FLAG_UP);   // released
      activeRule = -1;
    }
  } else {
    if (!released) {
      if ((uint32_t)(now - lastSendAt) >= NEC_REPEAT_MS) {
        LOG("  send: NEC full-frame repeat");
        // The RX-V557 accepts the initial NEC command but does not reliably
        // act on short NEC repeat codes. Repeat the complete command instead.
        lastSendAt = millis();              // schedule from the start of this full NEC frame
        sendNecFrame(r.outValue);
      }
    } else {
      LOG("  key released");
      activeRule = -1;                              // NEC has no "key up"
    }
  }
}

uint8_t lastBits = 0;
uint32_t lastValue = 0;
uint32_t lastUnknownAt = 0;

void onSony(uint8_t bits, uint32_t value) {
  uint32_t now = millis();
  int8_t found = -1;
  for (uint8_t i = 0; i < RULE_COUNT; i++) {
    if (RULES[i].sonyBits == bits && RULES[i].sonyValue == value) { found = i; break; }
  }

  if (found >= 0 && found == activeRule) {          // the same key is still down
    lastFrameAt = now;
    return;
  }

#if USE_SERIAL
  bool newUnknown = (bits != lastBits) || (value != lastValue) || ((uint32_t)(now - lastUnknownAt) > 250UL);
  if (found >= 0 || newUnknown) {
    Serial.print(F("SONY bits=")); Serial.print(bits);
    Serial.print(F(" value=0x")); Serial.print(value, HEX);
    Serial.println(found >= 0 ? F("  -> rule found") : F("  -> no rule (forwarded)"));
    Serial.flush();
  }
#endif
  lastBits = bits;
  lastValue = value;
  lastUnknownAt = now;

  if (found < 0) {
    if (activeUnknown && bits == unknownBits && value == unknownValue) {
      unknownLastFrameAt = now;                     // same unmapped key is still held
      return;
    }
    finishActive();                                 // stop any previous mapped key before pass-through
    activeUnknown = true;
    unknownBits = bits;
    unknownValue = value;
    unknownLastFrameAt = millis();              // start release timeout after any previous key is finished
    unknownLastSendAt = unknownLastFrameAt;
    unknownRawLen = rawLen;
    for (uint8_t i = 0; i < rawLen; i++) unknownRaw[i] = raw[i];
#if DEBUG_LED
    blink(3, 50);
#endif
    forwardUnknownSonyRaw();                        // preserve received mark/space timings
    return;
  }

  activeUnknown = false;                            // a mapped key replaced the unmapped key
  finishActive();                                   // a different key went down: complete the previous one first
#if DEBUG_LED
  blink(2, 150);
#endif
  activeRule = found;
  lastFrameAt = millis();
  if (RULES[found].repeat) startHeld(RULES[found]);
}

void handleFrame() {
  if (raw[0] > 1500 && raw[0] < 3500) {                 // Sony start mark (2400 us, wide window)
    uint8_t bits = (rawLen - 1) / 2;
    if (bits != 12 && bits != 15 && bits != 20) {
#if DEBUG_LED
      blink(1, 60);                                     // 1 blink = Sony-like frame with a wrong bit count
#endif
      return;
    }
    // The receiver can stretch or shorten marks. Use the midpoint of the shortest and longest mark
    // as the 0/1 threshold (nominally 600 us and 1200 us). Fall back to 900 us if all marks look alike.
    uint16_t mn = 65535, mx = 0;
    for (uint8_t i = 0; i != bits; i++) {
      uint16_t m = raw[2 + 2 * i];
      if (m < mn) mn = m;
      if (m > mx) mx = m;
    }
    uint16_t thr = (mx > mn + mn / 2) ? (mn + mx) / 2 : 900;
    uint32_t v = 0;
    for (uint8_t i = 0; i != bits; i++) {
      if (raw[2 + 2 * i] > thr) v |= 1UL << i;
    }
#if USE_SERIAL
    Serial.print(F("start=")); Serial.print(raw[0]);
    Serial.print(F(" marks min=")); Serial.print(mn);
    Serial.print(F(" max=")); Serial.print(mx);
    Serial.print(F(" thr=")); Serial.println(thr);
#endif
    onSony(bits, v);
  } else {
#if DEBUG_LED
    blink(1, 60);                                       // 1 blink = a frame that is not Sony
#endif
  }
}

void setup() {
#if USE_SERIAL
  Serial.begin(115200);
  Serial.println(F("IR converter ready"));
#endif
  pinMode(RX_PIN, INPUT);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);
  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, LOW);
  hardwareOn();
}

void loop() {
  if (digitalRead(RX_PIN) == LOW) {
    if (captureFrame()) handleFrame();
  }
  serviceActive();
}
