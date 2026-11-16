// ir_converter_smooth_serial | version 2.0 | build date 2026-11-06 | three remotes (Sony, Yamaha, Dreambox) | carrier: hardware timer (Timer3, pin 5)
#include <Arduino.h>
#include <avr/interrupt.h>
#include <avr/io.h>

// IR converter and repeater - Arduino Mega 2560, hardware carrier from Timer3
// Receives the Sony TV remote, the Yamaha amplifier remote and the Dreambox remote with one KY-022 receiver (pin 52).
// Sends to the Sony TV, the Yamaha amplifier and the Dreambox with one IR LED + driver (pin 5).
//
// For every key that is received:
//   - if it has a rule in the RULES table below, the rule is executed (conversion between remotes);
//   - if it has no rule, it is passed on unchanged to the device it belongs to:
//         Sony remote    -> Sony TV      (one press of 3 frames, 45 ms apart, after the key is released)
//         Yamaha remote  -> Yamaha amp   (NEC frame, repeated while the key is held)
//         Dreambox remote-> Dreambox     (down pair, repeat pairs every 118 ms, up pair)
//
// Two ways to place the Arduino:
//   A) next to the Yamaha amplifier and the Dreambox, their own IR windows covered: keep all three FORWARD_ options at 1.
//   B) away from them, where the Arduino only has to see the Sony remote: the original Yamaha and Dreambox remotes
//      reach their devices directly, so set FORWARD_YAMAHA_REMOTE and FORWARD_DREAMBOX_REMOTE to 0
//      (their keys with a rule in the table are still converted).
//
// Continuous keys (rule field repeat = true) repeat while held: the volume keys.
// The built-in LED (pin 13) is on while something is being transmitted.

#define USE_SERIAL   1     // 1 = print received codes on the Serial Monitor (115200 baud)
#define DEBUG_LED    0     // 1 = blink the built-in LED for a received key: 2 slow = rule found, 3 fast = no rule

#define FORWARD_SONY_REMOTE      1   // 1 = pass unmapped keys of the Sony remote on to the TV
#define FORWARD_YAMAHA_REMOTE    1   // 1 = pass unmapped keys of the Yamaha remote on to the amplifier
#define FORWARD_DREAMBOX_REMOTE  1   // 1 = pass unmapped keys of the Dreambox remote on to the Dreambox

#if USE_SERIAL
#define LOG(text) do { Serial.println(F(text)); Serial.flush(); } while (0)
#else
#define LOG(text) do { } while (0)
#endif

const uint8_t RX_PIN = 52;
const uint8_t TX_PIN = 5;              // OC3A / Timer3
const uint8_t STATUS_LED = 13;
const uint16_t CARRIER_KHZ = 38;             // Dreambox and Yamaha
const uint8_t  SONY_CARRIER_KHZ = 40;          // carrier for Sony codes: 40 kHz is the Sony standard (36 to 42 kHz all worked in the scan)

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
// header frames the Dreambox module accepts (index 0-3 and 7-11 of its table)
const uint32_t DB_HEADERS[] = { 0x0E0F441AUL, 0x0D1F441AUL, 0x0C2F441AUL, 0x0B3F441AUL, 0x2B1F441AUL,
                                0x3B0F441AUL, 0x3A1F441AUL, 0x392F441AUL, 0x383F441AUL };
const uint8_t DB_HEADER_COUNT = sizeof(DB_HEADERS) / sizeof(DB_HEADERS[0]);

// ---- NEC (Yamaha) ----
const uint16_t NEC_REPEAT_MS = 108;         // start of the frame / of a repeat code to the start of the next
const uint8_t  NEC_TAP_REPEATS = 0;         // repeat codes after the frame for single (non-repeat) rules

// ---- NEC hold behaviour of the Yamaha amplifier ----
// 1 = while a key is held, send the full NEC frame again every NEC_HOLD_FRAME_MS (the amplifier did not
//     step the volume continuously with the short NEC repeat codes);  0 = send short NEC repeat codes instead.
#define NEC_HOLD_FULL_FRAME 1
const uint16_t NEC_HOLD_FRAME_MS = 120;     // start of one full frame to the start of the next

// ---- Sony SIRC ----
// 1 = send one complete press (3 frames, exactly 45 ms apart) after the Sony key has been released: the same
//     way the frequency scan sent it, without overlap with the original remote. Holding such a key does not repeat it.
// 0 = start at once and keep sending a frame every 45 ms while the key is held (less exact timing).
#define SONY_FORWARD_AFTER_RELEASE 1
const uint16_t SONY_FRAME_MS = 45;          // frame start to frame start (SIRC)
const uint8_t  SONY_MIN_FRAMES = 3;         // SIRC commands are sent at least three times
const uint16_t RELEASE_FWD_MS = 150;        // no frame of that key for this long = released

// ---- when is a key released? (no frame of that key for this long) ----
const uint16_t RELEASE_SINGLE_MS = 120;     // keys with repeat = false (nothing is sent in the meantime)
const uint16_t RELEASE_HELD_MS = 200;       // Dreambox keys with repeat = true (our own sending makes us miss frames)
const uint16_t RELEASE_NEC_HELD_MS = 220;   // Yamaha keys with repeat = true
const uint16_t RELEASE_REMOTE_MS = 300;     // keys of the Yamaha and Dreambox remotes: their frames come only every 108-118 ms

// ---- Dreambox key codes ----
const uint8_t K_MENU = 32, K_EXIT = 82, K_OK = 37, K_LEFT = 35, K_RIGHT = 36;
const uint8_t K_VOL_UP = 10, K_VOL_DOWN = 11, K_CH_UP = 13, K_CH_DOWN = 14;
const uint8_t K_YELLOW = 66;
const uint8_t K_UP = 33, K_DOWN = 34;

// ---------------- the rules ----------------
enum In  : uint8_t { IN_SONY, IN_NEC, IN_DREAMBOX };            // which remote sends the key
enum Out : uint8_t { OUT_DREAMBOX, OUT_NEC, OUT_SONY };         // where the converted key goes

struct Rule {
  In       in;          // IN_SONY, IN_NEC (Yamaha remote) or IN_DREAMBOX
  uint8_t  inBits;      // Sony: 12, 15 or 20.  NEC: 32.  Dreambox: 0
  uint32_t inValue;     // Sony: value printed by the sketch.  NEC: the 32-bit word.  Dreambox: key code
  Out      out;         // OUT_DREAMBOX, OUT_NEC or OUT_SONY
  uint32_t outValue;    // Dreambox: key code (K_MENU ...).  NEC: 32-bit word.  Sony: value
  bool     repeat;      // true = continuous while the key is held (volume)
  uint8_t  outBits;     // OUT_SONY only: 12, 15 or 20 (leave out for the other outputs)
};

const Rule RULES[] = {
  // from,    bits, value,  to,           value,         continuous?
  // ---- Sony remote -> Yamaha amplifier (NEC) ----
  { IN_SONY, 12, 0x092,  OUT_NEC,      0xE51A857AUL,  true  },   // Sony Volume up   -> Yamaha Volume up   (continuous)
  { IN_SONY, 12, 0x093,  OUT_NEC,      0xE41B857AUL,  true  },   // Sony Volume down -> Yamaha Volume down (continuous)
  { IN_SONY, 12, 0x097,  OUT_NEC,      0x7788857AUL,  false },   // Sony Audio       -> Yamaha Stereo
  { IN_SONY, 15, 0x4BA8, OUT_NEC,      0x748B857AUL,  false },   // Sony Subtitle    -> Yamaha Movie
  { IN_SONY, 15, 0x624D, OUT_NEC,      0x6A95857AUL,  false },   // Sony Help        -> Yamaha Night
  { IN_SONY, 12, 0x094,  OUT_NEC,      0xE31C857AUL,  false },   // Sony Mute        -> Yamaha Mute
  { IN_SONY, 15, 0x0D73, OUT_NEC,      0xE21D857AUL,  false },   // Sony DVR         -> Yamaha Power on
  { IN_SONY, 15, 0x525B, OUT_NEC,      0xE11E857AUL,  false },   // Sony Guide       -> Yamaha Power off
  // ---- Sony remote -> Dreambox ----
  { IN_SONY, 12, 0x0BB,  OUT_DREAMBOX, K_EXIT,        false },   // Sony Jump        -> Dreambox Exit
  { IN_SONY, 15, 0x4BA7, OUT_DREAMBOX, K_VOL_DOWN,    true  },   // Sony Yellow      -> Dreambox Volume down (continuous)
  { IN_SONY, 15, 0x4BA4, OUT_DREAMBOX, K_DOWN,        false },   // Sony Blue        -> Dreambox Down
  { IN_SONY, 15, 0x4BA5, OUT_DREAMBOX, K_UP,          false },   // Sony Red         -> Dreambox Up
  { IN_SONY, 15, 0x4BA6, OUT_DREAMBOX, K_VOL_UP,      true  },   // Sony Green       -> Dreambox Volume up   (continuous)
  { IN_SONY, 15, 0x0D7C, OUT_DREAMBOX, K_OK,          false },   // Sony Netflix     -> Dreambox OK
  { IN_SONY, 15, 0x6246, OUT_DREAMBOX, K_YELLOW,      false },   // Sony Google Play -> Dreambox Yellow
  { IN_SONY, 12, 0x090,  OUT_DREAMBOX, K_RIGHT,       false },   // Sony CH+         -> Dreambox Right
  { IN_SONY, 12, 0x091,  OUT_DREAMBOX, K_LEFT,        false },   // Sony CH-         -> Dreambox Left
  { IN_SONY, 15, 0x3BCD, OUT_DREAMBOX, K_YELLOW,      false },   // Sony 3D          -> Dreambox Yellow

  // ---- Yamaha remote -> other devices (examples, remove the // to use them; learn the NEC word of a key from the Serial Monitor) ----
  // { IN_NEC, 32, 0xE31C857AUL, OUT_DREAMBOX, K_MENU,   false },   // Yamaha Mute     -> Dreambox Menu
  // { IN_NEC, 32, 0xE31C857AUL, OUT_SONY,     0x094,    false, 12 }, // Yamaha Mute     -> Sony TV Mute

  // ---- Dreambox remote -> other devices (examples; the key code is printed by the sketch) ----
  // { IN_DREAMBOX, 0, 37, OUT_NEC,  0xE51A857AUL, false },          // Dreambox OK     -> Yamaha Volume up
  // { IN_DREAMBOX, 0, 32, OUT_SONY, 0x094,        false, 12 },      // Dreambox Menu   -> Sony TV Mute
};
const uint8_t RULE_COUNT = sizeof(RULES) / sizeof(RULES[0]);

// An unmapped key of the Yamaha or Dreambox remote becomes a temporary rule that sends the same code back out.
Rule dynRule = { IN_NEC, 0, 0, OUT_NEC, 0, true, 0 };
const Rule &ruleAt(int8_t i) { return (i == (int8_t)RULE_COUNT) ? dynRule : RULES[i]; }
bool isHeld(const Rule &r) { return r.repeat && r.out != OUT_SONY; }

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
uint32_t txHeader = HEADER_FRAME;       // header frame used for sending (a forwarded key keeps the header of its remote)

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
  sendDreamboxFrame(txHeader);
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

// ---------------- Sony SIRC ----------------
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

// One complete Sony press: 3 frames, each started exactly SONY_FRAME_MS after the previous one (busy wait).
void sendSonyPress(uint8_t bits, uint32_t value) {
  setCarrier(SONY_CARRIER_KHZ);
  for (uint8_t f = 0; f < SONY_MIN_FRAMES; f++) {
    uint32_t t0 = millis();
    sendSonyFrame(bits, value);
    while ((uint32_t)(millis() - t0) < SONY_FRAME_MS) { }
  }
}

// Forwarding state: one unmapped Sony key at a time.
bool fwdActive = false;
bool fwdPending = false;       // the press has not been sent yet (waiting for the key release)
uint8_t fwdBits = 0;
uint32_t fwdValue = 0;
uint32_t fwdLastFrameAt = 0;   // time of the last received frame of that key
uint32_t fwdLastSendAt = 0;    // start time of the last frame we sent (hold mode only)

// Send a press that is still waiting, then go back to the normal carrier.
void finishForward() {
  if (!fwdActive) return;
  if (fwdPending) {
    LOG("  forward: unmapped Sony code re-sent unchanged");
    sendSonyPress(fwdBits, fwdValue);
    fwdPending = false;
  }
  fwdActive = false;
  setCarrier(CARRIER_KHZ);                          // back to the carrier of the other devices
}

// First frame of an unmapped Sony key.
void startForward(uint8_t bits, uint32_t value) {
  fwdActive = true;
  fwdBits = bits;
  fwdValue = value;
  fwdLastFrameAt = millis();
#if SONY_FORWARD_AFTER_RELEASE
  fwdPending = true;                                // sent when the key is released
#else
  fwdPending = false;
  LOG("  forward: unmapped Sony code re-sent unchanged");
  sendSonyPress(bits, value);                       // the 3 frames now, more while the key is held
  fwdLastSendAt = millis();
#endif
}

// Called on every pass of loop().
void serviceForward() {
  if (!fwdActive) return;
  uint32_t now = millis();
  bool released = (uint32_t)(now - fwdLastFrameAt) > RELEASE_FWD_MS;
#if SONY_FORWARD_AFTER_RELEASE
  if (released) finishForward();                    // sends the press and restores the carrier
#else
  if (!released) {
    if ((uint32_t)(now - fwdLastSendAt) >= SONY_FRAME_MS) {
      sendSonyFrame(fwdBits, fwdValue);
      fwdLastSendAt = now;
    }
  } else {
    finishForward();
  }
#endif
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

// NEC full frame (Yamaha remote): 9 ms + 4.5 ms leader, 32 bits.
bool decodeNec(uint32_t *word) {
  if (rawLen < 67) return false;
  uint32_t w = 0;
  for (uint8_t i = 0; i < 32; i++) {
    if (raw[3 + 2 * i] > 1100) w |= 1UL << i;       // 1688 us space = 1, 562 us space = 0
  }
  *word = w;
  return true;
}

// Dreambox frame: 9 short bursts, the 8 intervals between them are 8 nibbles (see docs/dreambox-ir-protocol.md).
bool decodeDreambox(uint32_t *word) {
  if (rawLen != 17 || raw[0] > 700) return false;
  uint32_t w = 0;
  uint8_t sum = 0;
  for (uint8_t i = 0; i < 8; i++) {
    uint32_t t = (uint32_t)raw[2 * i] + raw[2 * i + 1];   // start of one burst to the start of the next
    uint32_t units = t * 27UL / 2UL;                       // timer units used by the Dreambox module
    if (units <= 12188UL || units >= 41517UL) return false;
    uint8_t n = (uint8_t)(((units - 12189UL) / 1833UL) & 0x0F);
    w = (w << 4) | n;
    sum += n;
  }
  if (sum & 0x0F) return false;                            // the nibble sum must be a multiple of 16
  *word = w;
  return true;
}

bool isDreamboxHeader(uint32_t w) {
  for (uint8_t i = 0; i < DB_HEADER_COUNT; i++) {
    if (DB_HEADERS[i] == w) return true;
  }
  return false;
}

// ---------------- key state: one key at a time ----------------
int8_t activeRule = -1;        // rule of the key that is down right now, -1 = none, RULE_COUNT = temporary rule
uint32_t lastFrameAt = 0;      // time of the last frame of that key
uint32_t lastSendAt = 0;       // start time of the last code sent for that key
uint8_t sentRepeats = 0;       // Dreambox repeat pairs sent for that key
bool holdEvidence = false;     // the original key was seen again (second frame or repeat code)

uint16_t releaseMs(const Rule &r) {
  uint16_t t;
  if (!isHeld(r)) t = RELEASE_SINGLE_MS;
  else t = (r.out == OUT_DREAMBOX) ? RELEASE_HELD_MS : RELEASE_NEC_HELD_MS;
  if (r.in != IN_SONY && t < RELEASE_REMOTE_MS) t = RELEASE_REMOTE_MS;
  return t;
}

void runSingle(const Rule &r) {
  LOG("  send: single action");
  if (r.out == OUT_DREAMBOX) dreamboxSequence((uint8_t)r.outValue);
  else if (r.out == OUT_NEC) sendNecTap(r.outValue);
  else {
    sendSonyPress(r.outBits, r.outValue);
    setCarrier(CARRIER_KHZ);
  }
}

// Key goes down on a continuous rule: send the first code at once.
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
  const Rule &r = ruleAt(activeRule);
  if (!isHeld(r)) {
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
  if (activeRule < 0) return;
  const Rule &r = ruleAt(activeRule);
  uint32_t now = millis();
  uint32_t quiet = (uint32_t)(now - lastFrameAt);
  bool released = quiet > releaseMs(r);

  if (!isHeld(r)) {
    if (released) {                                 // key released: do the single action now
      runSingle(r);
      activeRule = -1;
    }
    return;
  }

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
      if (r.in == IN_NEC && !holdEvidence) return;  // a tap on the Yamaha remote sends one frame: do not repeat it
#if NEC_HOLD_FULL_FRAME
      if ((uint32_t)(now - lastSendAt) >= NEC_HOLD_FRAME_MS) {
        LOG("  send: NEC full frame again (key held)");
        sendNecFrame(r.outValue);
        lastSendAt = now;
      }
#else
      if ((uint32_t)(now - lastSendAt) >= NEC_REPEAT_MS) {
        LOG("  send: NEC repeat code");
        sendNecRepeat();
        lastSendAt = now;
      }
#endif
    } else {
      LOG("  key released");
      activeRule = -1;                              // NEC has no "key up"
    }
  }
}

// Make a rule the active one and start it.
void activate(int8_t idx) {
  const Rule &r = ruleAt(idx);
  activeRule = idx;
  lastFrameAt = millis();
  holdEvidence = false;
  if (isHeld(r)) startHeld(r);
}

uint8_t lastIn = 255;
uint8_t lastBits = 0;
uint32_t lastValue = 0;
uint32_t lastUnknownAt = 0;
uint32_t lastDbHeader = HEADER_FRAME;      // header of the last Dreambox remote key

// A key of any remote has been received.
void onKey(In in, uint8_t bits, uint32_t value) {
  uint32_t now = millis();
  int8_t found = -1;
  for (uint8_t i = 0; i < RULE_COUNT; i++) {
    if (RULES[i].in == in && RULES[i].inBits == bits && RULES[i].inValue == value) { found = i; break; }
  }

  if (activeRule >= 0) {                            // the same key is still down?
    const Rule &a = ruleAt(activeRule);
    if (a.in == in && a.inBits == bits && a.inValue == value) {
      lastFrameAt = now;
      holdEvidence = true;
      return;
    }
  }
  if (in == IN_SONY && found < 0 && fwdActive && bits == fwdBits && value == fwdValue) {
    fwdLastFrameAt = now;                           // the same unmapped Sony key is still down
    return;
  }

#if USE_SERIAL
  bool newKey = ((uint8_t)in != lastIn) || (bits != lastBits) || (value != lastValue) || ((uint32_t)(now - lastUnknownAt) > 250UL);
  if (found >= 0 || newKey) {
    if (in == IN_SONY) {
      Serial.print(F("SONY bits=")); Serial.print(bits);
      Serial.print(F(" value=0x")); Serial.print(value, HEX);
    } else if (in == IN_NEC) {
      Serial.print(F("YAMAHA remote  NEC word=0x")); Serial.print(value, HEX);
    } else {
      Serial.print(F("DREAMBOX remote  key=")); Serial.print(value);
      Serial.print(F(" header=0x")); Serial.print(lastDbHeader, HEX);
    }
    Serial.println(found >= 0 ? F("  -> rule found") : F("  -> no rule (passed on)"));
    Serial.flush();
  }
#endif
  lastIn = (uint8_t)in;
  lastBits = bits;
  lastValue = value;
  lastUnknownAt = now;

  if (found < 0) {
#if DEBUG_LED
    blink(3, 50);
#endif
    if (in == IN_SONY) {
#if FORWARD_SONY_REMOTE
      finishActive();                               // complete the previous mapped key first
      finishForward();
      startForward(bits, value);                    // no rule: send the Sony code on, unchanged
#endif
      return;
    }
    bool pass = (in == IN_NEC) ? (FORWARD_YAMAHA_REMOTE != 0) : (FORWARD_DREAMBOX_REMOTE != 0);
    if (!pass) return;
    finishForward();
    finishActive();
    dynRule.in = in;                                // identity rule: the same code goes back out
    dynRule.inBits = bits;
    dynRule.inValue = value;
    dynRule.out = (in == IN_NEC) ? OUT_NEC : OUT_DREAMBOX;
    dynRule.outValue = value;
    dynRule.repeat = true;
    dynRule.outBits = 0;
    txHeader = lastDbHeader;                        // keep the header of the Dreambox remote
    activate((int8_t)RULE_COUNT);
    return;
  }

  finishForward();                                  // a mapped key: stop forwarding an unmapped one, restore the carrier
  finishActive();                                   // a different key went down: complete the previous one first
#if DEBUG_LED
  blink(2, 150);
#endif
  txHeader = HEADER_FRAME;
  activate(found);
}

// A repeat code of the Yamaha remote: the last Yamaha key is still held.
void onNecRepeat() {
  if (activeRule < 0) return;
  if (ruleAt(activeRule).in != IN_NEC) return;
  lastFrameAt = millis();
  holdEvidence = true;
}

// The Dreambox remote has released a key.
void onDreamboxUp(uint8_t key) {
  if (activeRule < 0) return;
  const Rule &a = ruleAt(activeRule);
  if (a.in != IN_DREAMBOX || a.inValue != key) return;
  lastFrameAt = millis() - 1000UL;                  // released now
}

// Header frame + key frame make one Dreambox key event.
bool dbHaveHeader = false;
uint32_t dbHeader = 0;
uint32_t dbHeaderAt = 0;

void onDreamboxFrame(uint32_t w) {
  if (isDreamboxHeader(w)) {
    dbHeader = w;
    dbHeaderAt = millis();
    dbHaveHeader = true;                            // no printing here: the key frame follows 13 ms later
    return;
  }
  if ((w & 0xC06C80FFUL) != 0) return;              // not a key frame
  if (!dbHaveHeader || (uint32_t)(millis() - dbHeaderAt) > 60UL) return;
  dbHaveHeader = false;
  uint8_t key = (uint8_t)((w >> 8) & 0x7F);
  uint8_t flags = (uint8_t)((w >> 20) & 0x0F);
  lastDbHeader = dbHeader;
  if (flags == FLAG_UP) { onDreamboxUp(key); return; }
  if (flags != FLAG_DOWN && flags != FLAG_REPEAT) return;
  onKey(IN_DREAMBOX, 0, key);
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
    onKey(IN_SONY, bits, v);
  } else if (raw[0] > 7000 && raw[0] < 11000) {         // NEC leader (9 ms): Yamaha remote
    uint32_t w;
    if (rawLen >= 67) {
      if (decodeNec(&w)) onKey(IN_NEC, 32, w);
    } else if (rawLen <= 10 && raw[1] > 1500 && raw[1] < 3200) {
      onNecRepeat();                                    // 9 ms + 2.25 ms: NEC repeat code
    }
  } else if (raw[0] < 700 && rawLen == 17) {            // short bursts: Dreambox remote
    uint32_t w;
    if (decodeDreambox(&w)) onDreamboxFrame(w);
  } else {
#if DEBUG_LED
    blink(1, 60);                                       // 1 blink = a frame that is not recognised
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
  serviceForward();
}
