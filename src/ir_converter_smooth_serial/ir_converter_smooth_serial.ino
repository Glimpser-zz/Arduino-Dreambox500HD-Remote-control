// ir_converter_smooth_serial | version 3.1 | build date 2026-11-23 | IR converter (Sony, Yamaha, Dreambox) + climate control | IR carrier: hardware timer (Timer5, pin 45)
#include <Arduino.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>

// IR converter and repeater - Arduino Mega 2560, hardware carrier from Timer5
// Receives the Sony TV remote, the Yamaha amplifier remote and the Dreambox remote with one KY-022 receiver (pin 52).
// Sends to the Sony TV, the Yamaha amplifier and the Dreambox with one IR LED + driver (pin 45, Timer5 output OC5B).
//
// Version 3.0 adds a thermostat: AM2302 (DHT22) temperature and humidity sensor, a 2-channel relay module for heating and cooling,
// and the DFRobot LCD keypad shield for the display and the settings (see the CLIMATE section further down).
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
const uint8_t TX_PIN = 45;             // OC5B / Timer5
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

// Per-remote repeat controls; loaded from EEPROM with the climate settings.
bool remoteRepeatEnabled[3] = { true, true, true };  // Sony, Yamaha (NEC), Dreambox
bool isHeld(const Rule &r) { return r.repeat && r.out != OUT_SONY && remoteRepeatEnabled[(uint8_t)r.in]; }

// --------------------------------------------------
// Timer5 hardware PWM on pin 45 (OC5B)
// --------------------------------------------------

uint8_t currentCarrierKhz = 0;

// Same timer set-up as the tested sketch, for any carrier frequency in whole kHz.
void setCarrier(uint8_t khz) {
  if (khz == currentCarrierKhz) return;

  uint8_t oldSREG = SREG;
  cli();

  // Stop and disconnect Timer5.
  TCCR5A = 0;
  TCCR5B = 0;

  // Fast PWM mode 14, TOP = ICR5, prescaler = 1.
  ICR5 = (uint16_t)(F_CPU / (khz * 1000UL)) - 1;
  OCR5B = (ICR5 + 1) / 3;
  TCNT5 = 0;

  // Start Timer5 with output disconnected.
  TCCR5A = _BV(WGM31);
  TCCR5B = _BV(WGM33) | _BV(WGM32) | _BV(CS30);

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
  TCNT5 = 0;

  // Connect OC5B in non-inverting PWM mode.
  TCCR5A |= _BV(COM5B1);

  SREG = oldSREG;

  delayMicroseconds(us);

  oldSREG = SREG;
  cli();

  // Disconnect OC5B at the end of the burst.
  TCCR5A &= ~(_BV(COM5B1) | _BV(COM5B0));

  SREG = oldSREG;

  // Hold pin 45 LOW between bursts.
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
  } else if (raw[0] < 700 && rawLen >= 10 && rawLen <= 25) {   // short bursts: Dreambox remote
    uint32_t w;
    if (rawLen == 17 && decodeDreambox(&w)) {
      onDreamboxFrame(w);
    } else {
#if USE_SERIAL
      Serial.print(F("DREAMBOX-like frame NOT decoded, durations ")); Serial.print(rawLen); Serial.print(F(":"));
      for (uint8_t i = 0; i < rawLen; i++) { Serial.print(' '); Serial.print(raw[i]); }
      Serial.println();
#endif
    }
  } else {
#if DEBUG_LED
    blink(1, 60);                                       // 1 blink = a frame that is not recognised
#endif
  }
}


// ======================================================================
//  CLIMATE: AM2302 (DHT22) sensor, 2-channel relay module, LCD keypad shield
// ======================================================================
// Pins (everything else is used by the IR part: 52 receiver, 3 IR LED, 13 built-in LED):
//   LCD keypad shield : pins 4, 5, 6, 7, 8, 9 (LCD), 10 (backlight, left alone), A0 (buttons)
//   AM2302 data       : pin 22   (+5 V, GND; a 10 kohm pull-up between data and +5 V if the module has none)
//   relay IN1         : pin 23   heating
//   relay IN2         : pin 24   cooling
// Buttons: LEFT / RIGHT change the page; on the main page UP / DOWN change the set temperature and SELECT changes
// the mode (Off, Winter = heating, Summer = cooling); on the settings page SELECT chooses the field, UP / DOWN change it.
// Settings also include independent repeat switches for Sony, Yamaha and Dreambox, plus a master climate-control switch.
// The IR part always has priority: the sensor is read and the display is written only while no remote key is being handled.

#define CLIMATE_ENABLED 1              // 0 = IR converter only
#define RELAY_ACTIVE_LOW 1             // the usual 2-channel relay module switches ON when its input is LOW

#if CLIMATE_ENABLED

const uint8_t DHT_PIN = 22;
const uint8_t RELAY_HEAT_PIN = 23;
const uint8_t RELAY_COOL_PIN = 24;
const uint8_t BUTTON_PIN = A0;
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);   // RS, EN, D4, D5, D6, D7 of the DFRobot LCD keypad shield

const uint16_t SENSOR_PERIOD_MS = 5000;
const uint32_t SENSOR_FAIL_MS = 120000UL;   // no good reading for this long: both relays are switched off

enum { MODE_OFF = 0, MODE_WINTER = 1, MODE_SUMMER = 2 };

// ---------------- settings in EEPROM ----------------
struct Settings {
  uint16_t magic;
  uint8_t  version;
  uint8_t  mode;          // MODE_OFF, MODE_WINTER (heating), MODE_SUMMER (cooling)
  int16_t  setpoint;      // tenths of a degree Celsius
  int16_t  hysteresis;    // tenths of a degree
  int16_t  offset;        // sensor offset, tenths of a degree
  uint16_t minOnS;        // minimum time a relay stays on, seconds
  uint16_t minOffS;       // minimum time a relay stays off, seconds
  uint8_t  repeatSony;    // repeat mapped Sony remote commands while held
  uint8_t  repeatYamaha;  // repeat mapped/forwarded Yamaha commands while held
  uint8_t  repeatDreambox;// repeat mapped/forwarded Dreambox commands while held
  uint8_t  climateEnabled;// master enable for summer/winter thermostat and both relays
  uint16_t checksum;
};

// Previous EEPROM layout, retained for a one-time migration without losing thermostat settings.
struct LegacySettings {
  uint16_t magic;
  uint8_t  version;
  uint8_t  mode;
  int16_t  setpoint;
  int16_t  hysteresis;
  int16_t  offset;
  uint16_t minOnS;
  uint16_t minOffS;
  uint16_t checksum;
};
const uint16_t SETTINGS_MAGIC = 0xC1A7;
const uint8_t SETTINGS_VERSION = 2;
Settings cfg;
bool climateControlEnabled = true;

// Byte-pointer parameters avoid Arduino IDE auto-prototypes referring to Settings
// before the structure definition is visible to the compiler.
uint16_t settingsChecksum(const uint8_t *p) {
  uint16_t sum = 0x55AA;
  for (uint8_t i = 0; i < sizeof(Settings) - 2; i++) sum = (uint16_t)((sum << 1) | (sum >> 15)) ^ p[i];
  return sum;
}

bool settingsValid(const uint8_t *data) {
  const Settings &s = *(const Settings *)data;
  return s.magic == SETTINGS_MAGIC && s.version == SETTINGS_VERSION && s.mode <= MODE_SUMMER &&
         s.setpoint >= 50 && s.setpoint <= 350 && s.hysteresis >= 1 && s.hysteresis <= 100 &&
         s.offset >= -100 && s.offset <= 100 && s.minOnS <= 1800 && s.minOffS <= 3600 &&
         s.repeatSony <= 1 && s.repeatYamaha <= 1 && s.repeatDreambox <= 1 && s.climateEnabled <= 1 &&
         s.checksum == settingsChecksum(data);
}

void settingsDefaults() {
  cfg.magic = SETTINGS_MAGIC;
  cfg.version = SETTINGS_VERSION;
  cfg.mode = MODE_OFF;          // safe first start: nothing is switched until the mode is chosen
  cfg.setpoint = 220;           // 22.0 C
  cfg.hysteresis = 5;           // 0.5 C
  cfg.offset = 0;
  cfg.minOnS = 30;
  cfg.minOffS = 120;
  cfg.repeatSony = 1;
  cfg.repeatYamaha = 1;
  cfg.repeatDreambox = 1;
  cfg.climateEnabled = 1;
  cfg.checksum = 0;
}

uint16_t legacySettingsChecksum(const uint8_t *p) {
  uint16_t sum = 0x55AA;
  for (uint8_t i = 0; i < sizeof(LegacySettings) - 2; i++) sum = (uint16_t)((sum << 1) | (sum >> 15)) ^ p[i];
  return sum;
}

bool legacySettingsValid(const LegacySettings &s) {
  return s.magic == SETTINGS_MAGIC && s.version == 1 && s.mode <= MODE_SUMMER &&
         s.setpoint >= 50 && s.setpoint <= 350 && s.hysteresis >= 1 && s.hysteresis <= 100 &&
         s.offset >= -100 && s.offset <= 100 && s.minOnS <= 1800 && s.minOffS <= 3600 &&
         s.checksum == legacySettingsChecksum((const uint8_t *)&s);
}

void applySettings() {
  remoteRepeatEnabled[0] = cfg.repeatSony != 0;
  remoteRepeatEnabled[1] = cfg.repeatYamaha != 0;
  remoteRepeatEnabled[2] = cfg.repeatDreambox != 0;
  climateControlEnabled = cfg.climateEnabled != 0;
}

void settingsLoad() {
  EEPROM.get(0, cfg);
  if (settingsValid((const uint8_t *)&cfg)) {
    applySettings();
    return;
  }

  LegacySettings old;
  EEPROM.get(0, old);
  if (legacySettingsValid(old)) {
    settingsDefaults();
    cfg.mode = old.mode;
    cfg.setpoint = old.setpoint;
    cfg.hysteresis = old.hysteresis;
    cfg.offset = old.offset;
    cfg.minOnS = old.minOnS;
    cfg.minOffS = old.minOffS;
    settingsSave();
  } else {
    settingsDefaults();
  }
  applySettings();
}

void settingsSave() {
  cfg.repeatSony = remoteRepeatEnabled[0] ? 1 : 0;
  cfg.repeatYamaha = remoteRepeatEnabled[1] ? 1 : 0;
  cfg.repeatDreambox = remoteRepeatEnabled[2] ? 1 : 0;
  cfg.climateEnabled = climateControlEnabled ? 1 : 0;
  cfg.checksum = settingsChecksum((const uint8_t *)&cfg);
  EEPROM.put(0, cfg);           // writes only the bytes that changed
}

bool settingsDirty = false;
uint32_t settingsDirtyAt = 0;
void markDirty() { settingsDirty = true; settingsDirtyAt = millis(); }

// ---------------- AM2302 / DHT22 ----------------
// Counts how many loop passes the line stays at the given level; 0 = timeout.
uint16_t dhtPulse(uint8_t level) {
  uint16_t count = 0;
  while (digitalRead(DHT_PIN) == level) {
    if (++count > 400) return 0;
  }
  return count;
}

// Reads temperature and humidity in tenths. Takes about 5 ms with interrupts off, so call it only while the IR part is idle.
bool dhtRead(int16_t *tTenths, int16_t *hTenths) {
  uint8_t data[5] = { 0, 0, 0, 0, 0 };

  pinMode(DHT_PIN, OUTPUT);
  digitalWrite(DHT_PIN, LOW);
  delay(2);                                       // start signal: low for at least 1 ms
  digitalWrite(DHT_PIN, HIGH);
  delayMicroseconds(30);
  pinMode(DHT_PIN, INPUT_PULLUP);

  noInterrupts();
  bool ok = (dhtPulse(HIGH) != 0)                 // the line is still high for 20 to 40 us
         && (dhtPulse(LOW) != 0)                  // answer of the sensor: 80 us low
         && (dhtPulse(HIGH) != 0);                // then 80 us high
  for (uint8_t i = 0; ok && i < 40; i++) {
    uint16_t low = dhtPulse(LOW);                 // every bit starts with 50 us low
    uint16_t high = dhtPulse(HIGH);               // 26 us high = 0, 70 us high = 1
    if (low == 0 || high == 0) { ok = false; break; }
    data[i / 8] = (uint8_t)(data[i / 8] << 1);
    if (high > low) data[i / 8] |= 1;
  }
  interrupts();
  if (!ok) return false;

  if (data[4] != (uint8_t)(data[0] + data[1] + data[2] + data[3])) return false;     // checksum
  int16_t h = (int16_t)(((uint16_t)data[0] << 8) | data[1]);
  int16_t t = (int16_t)(((uint16_t)(data[2] & 0x7F) << 8) | data[3]);
  if (data[2] & 0x80) t = -t;
  if (h < 0 || h > 1000 || t < -400 || t > 800) return false;
  *tTenths = t;
  *hTenths = h;
  return true;
}

// ---------------- readings and the 24 hour history ----------------
int16_t rawT = 0;                     // last good temperature, tenths, without the offset
int16_t curH = 0;                     // last good humidity, tenths of a percent
bool haveReading = false;
uint32_t lastGoodAt = 0;              // millis() of the last good reading (boot time before the first one)
uint32_t lastSampleAt = 0;
uint8_t sensorFails = 0;

int16_t curTemp() { return rawT + cfg.offset; }

// One bin per hour, 24 bins. The history is in RAM: it starts again after a power loss.
const uint8_t HIST_BINS = 24;
int16_t hTmin[HIST_BINS], hTmax[HIST_BINS], hHmin[HIST_BINS], hHmax[HIST_BINS];
uint8_t histIndex = 0;                // bin of the current hour
uint8_t histStarted = 1;              // number of bins that have been started (up to 24)
uint32_t histHourStart = 0;           // millis() at the start of the current hour

void histClear(uint8_t i) { hTmin[i] = 32767; hTmax[i] = -32768; hHmin[i] = 32767; hHmax[i] = -32768; }

void histAdd(int16_t t, int16_t h) {
  if (t < hTmin[histIndex]) hTmin[histIndex] = t;
  if (t > hTmax[histIndex]) hTmax[histIndex] = t;
  if (h < hHmin[histIndex]) hHmin[histIndex] = h;
  if (h > hHmax[histIndex]) hHmax[histIndex] = h;
}

void histService() {
  while ((uint32_t)(millis() - histHourStart) >= 3600000UL) {
    histHourStart += 3600000UL;
    histIndex = (uint8_t)((histIndex + 1) % HIST_BINS);
    histClear(histIndex);                                  // the oldest hour is dropped
    if (histStarted < HIST_BINS) histStarted++;
  }
}

// Minimum and maximum over the last 24 hours (or since the start, if it was less than 24 hours ago).
bool histRange(int16_t *tMin, int16_t *tMax, int16_t *hMin, int16_t *hMax) {
  int16_t a = 32767, b = -32768, c = 32767, d = -32768;
  for (uint8_t i = 0; i < HIST_BINS; i++) {
    if (hTmin[i] < a) a = hTmin[i];
    if (hTmax[i] > b) b = hTmax[i];
    if (hHmin[i] < c) c = hHmin[i];
    if (hHmax[i] > d) d = hHmax[i];
  }
  if (a == 32767) return false;
  *tMin = a; *tMax = b; *hMin = c; *hMax = d;
  return true;
}

// How much time the history covers, in minutes (at most 24 hours).
uint16_t histMinutes() {
  uint32_t m = (uint32_t)(histStarted - 1) * 60UL + (uint32_t)(millis() - histHourStart) / 60000UL;
  return (uint16_t)(m > 1440UL ? 1440UL : m);
}

// ---------------- thermostat and relays ----------------
bool heatOn = false, coolOn = false;
uint32_t heatChangedAt = 0, coolChangedAt = 0;      // millis() of the last switch; at boot it starts the minimum OFF time

void relayWrite(uint8_t pin, bool on) {
#if RELAY_ACTIVE_LOW
  digitalWrite(pin, on ? LOW : HIGH);
#else
  digitalWrite(pin, on ? HIGH : LOW);
#endif
}

void setHeat(bool on) { if (on == heatOn) return; heatOn = on; heatChangedAt = millis(); relayWrite(RELAY_HEAT_PIN, on); }
void setCool(bool on) { if (on == coolOn) return; coolOn = on; coolChangedAt = millis(); relayWrite(RELAY_COOL_PIN, on); }
void forceOff() { setHeat(false); setCool(false); }

bool canTurnOn(uint32_t changedAt)  { return (uint32_t)(millis() - changedAt) >= (uint32_t)cfg.minOffS * 1000UL; }
bool canTurnOff(uint32_t changedAt) { return (uint32_t)(millis() - changedAt) >= (uint32_t)cfg.minOnS * 1000UL; }

bool sensorFresh() { return haveReading && (uint32_t)(millis() - lastGoodAt) < SENSOR_FAIL_MS; }

// Heating turns on below (set - hysteresis) and off at the set temperature.
// Cooling turns on above (set + hysteresis) and off at the set temperature.
// Both relays stay off in mode Off and while there is no good sensor reading.
void thermostatService() {
  static uint32_t lastRun = 0;
  uint32_t now = millis();
  if ((uint32_t)(now - lastRun) < 1000UL) return;
  lastRun = now;

  if (!climateControlEnabled || !sensorFresh() || cfg.mode == MODE_OFF) { forceOff(); return; }
  int16_t t = curTemp();
  if (cfg.mode == MODE_WINTER) {
    setCool(false);
    if (!heatOn) { if (t <= cfg.setpoint - cfg.hysteresis && canTurnOn(heatChangedAt)) setHeat(true); }
    else         { if (t >= cfg.setpoint && canTurnOff(heatChangedAt)) setHeat(false); }
  } else {
    setHeat(false);
    if (!coolOn) { if (t >= cfg.setpoint + cfg.hysteresis && canTurnOn(coolChangedAt)) setCool(true); }
    else         { if (t <= cfg.setpoint && canTurnOff(coolChangedAt)) setCool(false); }
  }
}

void sensorService() {
  uint32_t now = millis();
  if ((uint32_t)(now - lastSampleAt) < SENSOR_PERIOD_MS) return;
  lastSampleAt = now;
  int16_t t, h;
  if (dhtRead(&t, &h)) {
    rawT = t;
    curH = h;
    haveReading = true;
    lastGoodAt = now;
    sensorFails = 0;
    histAdd(curTemp(), h);
  } else if (sensorFails < 255) {
    sensorFails++;
  }
}

// ---------------- buttons ----------------
enum { BTN_NONE = 0, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_SELECT };

uint8_t readButton() {
  int v = analogRead(BUTTON_PIN);
  if (v < 50) return BTN_RIGHT;
  if (v < 195) return BTN_UP;
  if (v < 380) return BTN_DOWN;
  if (v < 555) return BTN_LEFT;
  if (v < 790) return BTN_SELECT;
  return BTN_NONE;
}

// Returns a button once when it is pressed; UP and DOWN repeat while they are held.
uint8_t buttonEvent() {
  static uint8_t lastRaw = BTN_NONE, stable = BTN_NONE;
  static uint32_t rawAt = 0, lastPoll = 0, repeatAt = 0;
  uint32_t now = millis();
  if ((uint32_t)(now - lastPoll) < 10UL) return BTN_NONE;
  lastPoll = now;
  uint8_t raw = readButton();
  if (raw != lastRaw) { lastRaw = raw; rawAt = now; return BTN_NONE; }
  if ((uint32_t)(now - rawAt) < 30UL) return BTN_NONE;        // debounce
  if (raw != stable) {
    stable = raw;
    if (raw != BTN_NONE) { repeatAt = now + 500UL; return raw; }
    return BTN_NONE;
  }
  if ((raw == BTN_UP || raw == BTN_DOWN) && (int32_t)(now - repeatAt) >= 0) {
    repeatAt = now + 120UL;
    return raw;
  }
  return BTN_NONE;
}

// ---------------- pages and settings ----------------
uint8_t page = 0;                     // 0 main, 1 temperature 24 h, 2 humidity 24 h, 3 settings
uint8_t field = 0;                    // settings field
uint32_t lastButtonAt = 0;
const uint8_t FIELD_COUNT = 10;

void changeSetpoint(int16_t d) {
  long v = (long)cfg.setpoint + d;
  cfg.setpoint = (int16_t)(v < 50 ? 50 : (v > 350 ? 350 : v));
  markDirty();
}

void changeMode(int8_t dir) {
  cfg.mode = (uint8_t)((cfg.mode + 3 + dir) % 3);
  forceOff();                                       // a mode change always starts with both relays off
  markDirty();
}

void changeField(int8_t dir) {
  long v;
  switch (field) {
    case 0: changeMode(dir); return;
    case 1: changeSetpoint(dir * 5); return;
    case 2: v = (long)cfg.hysteresis + dir;      cfg.hysteresis = (int16_t)(v < 1 ? 1 : (v > 100 ? 100 : v)); break;
    case 3: v = (long)cfg.offset + dir;          cfg.offset = (int16_t)(v < -100 ? -100 : (v > 100 ? 100 : v)); break;
    case 4: v = (long)cfg.minOnS + dir * 10L;    cfg.minOnS = (uint16_t)(v < 0 ? 0 : (v > 1800 ? 1800 : v)); break;
    case 5: v = (long)cfg.minOffS + dir * 30L;  cfg.minOffS = (uint16_t)(v < 0 ? 0 : (v > 3600 ? 3600 : v)); break;
    case 6: remoteRepeatEnabled[0] = (dir > 0); break;
    case 7: remoteRepeatEnabled[1] = (dir > 0); break;
    case 8: remoteRepeatEnabled[2] = (dir > 0); break;
    default:
      climateControlEnabled = (dir > 0);
      if (!climateControlEnabled) forceOff();
      break;
  }
  markDirty();
}

void uiButtons() {
  uint8_t b = buttonEvent();
  uint32_t now = millis();
  if (b == BTN_NONE) {
    if (page != 0 && (uint32_t)(now - lastButtonAt) > 30000UL) page = 0;     // back to the main page
    return;
  }
  lastButtonAt = now;
  if (b == BTN_LEFT)  { page = (uint8_t)((page + 3) % 4); return; }
  if (b == BTN_RIGHT) { page = (uint8_t)((page + 1) % 4); return; }
  if (page == 0) {
    if (b == BTN_UP)     changeSetpoint(5);
    if (b == BTN_DOWN)   changeSetpoint(-5);
    if (b == BTN_SELECT) changeMode(1);
  } else if (page == 3) {
    if (b == BTN_SELECT) field = (uint8_t)((field + 1) % FIELD_COUNT);
    else if (b == BTN_UP) changeField(1);
    else if (b == BTN_DOWN) changeField(-1);
  }
}

// ---------------- display ----------------
char shownLine[2][17];

void fmtTenths(char *out, size_t n, int16_t v) {
  const char *sign = "";
  if (v < 0) { sign = "-"; v = (int16_t)-v; }
  snprintf(out, n, "%s%d.%d", sign, v / 10, v % 10);
}

void showLine(uint8_t row, const char *text) {
  char line[17];
  snprintf(line, sizeof(line), "%-16s", text);               // pad with spaces so nothing old stays on the line
  if (strcmp(line, shownLine[row]) != 0) {
    lcd.setCursor(0, row);
    lcd.print(line);
    strcpy(shownLine[row], line);
  }
}

void coverageText(char *out, size_t n) {
  uint16_t m = histMinutes();
  if (m >= 1425) snprintf(out, n, "  24h");
  else snprintf(out, n, "%2dh%02dm", m / 60, m % 60);
}

void uiDraw() {
  char l0[24], l1[24], a[12], b[12], c[8];
  if (page == 0) {
    if (!sensorFresh()) {
      snprintf(l0, sizeof(l0), "T --.-C  H --%%");
      if (!haveReading && (uint32_t)(millis() - lastGoodAt) < 15000UL) snprintf(l1, sizeof(l1), "Reading sensor..");
      else snprintf(l1, sizeof(l1), "SENSOR ERROR");
    } else {
      fmtTenths(a, sizeof(a), curTemp());
      snprintf(l0, sizeof(l0), "T %sC  H %d%%", a, (curH + 5) / 10);
      fmtTenths(b, sizeof(b), cfg.setpoint);
      if (cfg.mode == MODE_OFF) snprintf(l1, sizeof(l1), "Off  Set %s", b);
      else snprintf(l1, sizeof(l1), "%s %s %s:%s", cfg.mode == MODE_WINTER ? "Win" : "Sum", b,
                    cfg.mode == MODE_WINTER ? "HEAT" : "COOL", (cfg.mode == MODE_WINTER ? heatOn : coolOn) ? "ON" : "--");
    }
  } else if (page == 1 || page == 2) {
    int16_t tMin, tMax, hMin, hMax;
    coverageText(c, sizeof(c));
    snprintf(l0, sizeof(l0), "%s min/max %s", page == 1 ? "T" : "H", c);
    if (!histRange(&tMin, &tMax, &hMin, &hMax)) {
      snprintf(l1, sizeof(l1), "no data yet");
    } else if (page == 1) {
      fmtTenths(a, sizeof(a), tMin);
      fmtTenths(b, sizeof(b), tMax);
      snprintf(l1, sizeof(l1), "%s / %s C", a, b);
    } else {
      snprintf(l1, sizeof(l1), "%d / %d %%", (hMin + 5) / 10, (hMax + 5) / 10);
    }
  } else {
    static const char *const names[FIELD_COUNT] = { "Mode", "Setpoint", "Hysteresis", "Offset", "Min ON", "Min OFF", "Sony repeat", "Yam repeat", "DB repeat", "Climate ctrl" };
    snprintf(l0, sizeof(l0), "%d/%d %s", field + 1, FIELD_COUNT, names[field]);
    switch (field) {
      case 0: snprintf(l1, sizeof(l1), "%s", cfg.mode == MODE_OFF ? "OFF" : (cfg.mode == MODE_WINTER ? "WINTER heating" : "SUMMER cooling")); break;
      case 1: fmtTenths(a, sizeof(a), cfg.setpoint);   snprintf(l1, sizeof(l1), "%s C", a); break;
      case 2: fmtTenths(a, sizeof(a), cfg.hysteresis); snprintf(l1, sizeof(l1), "+/- %s C", a); break;
      case 3: fmtTenths(a, sizeof(a), cfg.offset);     snprintf(l1, sizeof(l1), "%s C", a); break;
      case 4: snprintf(l1, sizeof(l1), "%u s", cfg.minOnS); break;
      case 5: snprintf(l1, sizeof(l1), "%u s", cfg.minOffS); break;
      case 6: snprintf(l1, sizeof(l1), "%s", remoteRepeatEnabled[0] ? "ON" : "OFF"); break;
      case 7: snprintf(l1, sizeof(l1), "%s", remoteRepeatEnabled[1] ? "ON" : "OFF"); break;
      case 8: snprintf(l1, sizeof(l1), "%s", remoteRepeatEnabled[2] ? "ON" : "OFF"); break;
      default: snprintf(l1, sizeof(l1), "%s", climateControlEnabled ? "ON" : "OFF"); break;
    }
  }
  showLine(0, l0);
  showLine(1, l1);
}

// ---------------- the two service functions called from loop() ----------------
// Cheap, never blocks: runs on every pass.
void climateFast() {
  histService();
  thermostatService();
  uiButtons();
  if (settingsDirty && (uint32_t)(millis() - settingsDirtyAt) > 3000UL) {   // save 3 s after the last change
    settingsSave();
    settingsDirty = false;
  }
}

// Blocks for a few milliseconds (sensor, display): runs only while the IR part is idle.
void climateSlow() {
  sensorService();
  uiDraw();
#if USE_SERIAL
  static uint32_t lastPrint = 0;
  uint32_t now = millis();
  if ((uint32_t)(now - lastPrint) >= 30000UL) {
    lastPrint = now;
    char a[12];
    fmtTenths(a, sizeof(a), curTemp());
    Serial.print(F("CLIMATE T=")); Serial.print(a);
    Serial.print(F(" H=")); Serial.print((curH + 5) / 10);
    Serial.print(F(" mode=")); Serial.print(cfg.mode == MODE_OFF ? F("off") : (cfg.mode == MODE_WINTER ? F("winter") : F("summer")));
    Serial.print(F(" heat=")); Serial.print(heatOn ? F("ON") : F("off"));
    Serial.print(F(" cool=")); Serial.println(coolOn ? F("ON") : F("off"));
  }
#endif
}

void climateSetup() {
  digitalWrite(RELAY_HEAT_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);     // relays OFF before the pins become outputs
  digitalWrite(RELAY_COOL_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);
  pinMode(RELAY_HEAT_PIN, OUTPUT);
  pinMode(RELAY_COOL_PIN, OUTPUT);
  settingsLoad();
  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print("IR hub + climate");
  lcd.setCursor(0, 1);
  lcd.print("version 3.1");
  delay(1200);
  uint32_t now = millis();
  heatChangedAt = coolChangedAt = lastGoodAt = lastSampleAt = histHourStart = now;
  for (uint8_t i = 0; i < HIST_BINS; i++) histClear(i);
  shownLine[0][0] = shownLine[1][0] = 0;
}

#endif   // CLIMATE_ENABLED

// True when no remote key is being handled and nothing is being received: then the sensor and the display may block for a few milliseconds.
uint32_t lastRxAt = 0;
bool irIdle() {
  return activeRule < 0 && !fwdActive && !(dbHaveHeader && (uint32_t)(millis() - dbHeaderAt) < 100UL) &&
         digitalRead(RX_PIN) == HIGH && (uint32_t)(millis() - lastRxAt) > 30UL;
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
#if CLIMATE_ENABLED
  climateSetup();
#endif
}

void loop() {
  if (digitalRead(RX_PIN) == LOW) {
    lastRxAt = millis();
    if (captureFrame()) handleFrame();
  }
  serviceActive();
  serviceForward();
#if CLIMATE_ENABLED
  climateFast();
  if (irIdle()) climateSlow();
#endif
}
