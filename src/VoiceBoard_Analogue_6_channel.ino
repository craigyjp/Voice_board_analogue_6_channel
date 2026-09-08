/*
 * MIDI CC to CV/Gate Converter
 * ============================================================
 * Hardware:
 *   - 4x DAC8568 (8-channel, 16-bit SPI DAC) → 32 CV outputs (0–5 V)
 *   - Shift registers via Roxmux             → 24 gate/switch outputs (0 / 5 V)
 *   - MIDI input (Serial or SoftwareSerial)
 *
 * Pin assignment (Seeed XIAO RA4M1 / compatible):
 *   D8 = SCK, D9 = MISO, D10 = MOSI  ← hardware SPI, do NOT use as CS
 *   D0, D1, D2, D6  → DAC8568 CS0, CS1, CS2, CS3
 *   D3, D4, D5  → 74HC595 DATA, CLOCK, LATCH
 *
 * Libraries required (install via Library Manager):
 *   - MIDI Library          (FortySevenEffects/arduino-MIDI-library)
 *   - ShiftRegister74HC595  (Timo Denk — search "ShiftRegister74HC595")
 *
 * Configuration:
 *   Edit the CONFIG section below to match your wiring and
 *   which MIDI channels / CC numbers map to which output.
 * ============================================================
 */

#include <MIDI.h>
#include <SPI.h>
#include <ShiftRegister74HC595.h>

// CV voltages CC number

#define VCF_ATTACK 9
#define VCF_DECAY 10
#define VCF_SUSTAIN 11
#define VCF_RELEASE 12
#define VCA_ATTACK 13
#define VCA_DECAY 14
#define VCA_SUSTAIN 15
#define VCA_RELEASE 16

#define FILTER_CUTOFF 19
#define FILTER_RES 20
#define VOLUME 23
#define EFFECT_POT1 25
#define EFFECT_POT2 26
#define EFFECT_POT3 27
#define EFFECT_MIX 28
#define NOISE_LEVEL 29

#define EG_DEPTH 32

#define OSC1_SAW_LEVEL 33
#define OSC1_PULSE_LEVEL 34
#define OSC1_SUB_LEVEL 35
#define OSC2_SAW_LEVEL 36
#define OSC2_PULSE_LEVEL 37
#define OSC2_TRIANGLE_LEVEL 38
#define OSC1_PM_DCO 39
#define OSC1_PM_ENV 40


// Switches CC number

#define FILTER_EG_INVERT 68
#define FILTER_VELOCITY 69
#define AMP_VELOCITY 70
#define FILTER_POLE 71
#define FILTER_LIN_LOG 72
#define AMP_LIN_LOG 73
#define POLYMOD_DEST_DCO1 74
#define POLYMOD_DEST_FILTER 75

#define EFFECT_BANK_1 76
#define EFFECT_BANK_2 77
#define EFFECT_BANK_3 78
#define EFFECT_2 79
#define EFFECT_1 80
#define EFFECT_0 81
#define EFFECT_INTERNAL 82
#define FILTER_PUNCH 83

#define AMP_PUNCH 83
#define FILTER_A 85
#define FILTER_B 86
#define FILTER_C 87
#define FILTER_MODE_BIT0 88
#define FILTER_MODE_BIT1 89
#define AMP_MODE_BIT0 90
#define AMP_MODE_BIT1 91

// ============================================================
//  CONFIG — edit this section to match your setup
// ============================================================

// --- MIDI channels to listen on (1–16). Set MIDI_CHANNEL_B to
//     0 to disable the second channel.
static const uint8_t MIDI_CHANNEL_A = 10;
static const uint8_t MIDI_CHANNEL_B = 0;   // set to 0 to disable

// --- DAC8568 SPI chip-select pins (one per IC)
//     D8/D9/D10 are SCK/MISO/MOSI on this board — keep them free for SPI.
//     IC0 → CCs mapped to DAC channels 0–7
//     IC1 → CCs mapped to DAC channels 8–15
//     IC2 → CCs mapped to DAC channels 16–23
static const uint8_t DAC_CS_PINS[4] = { 0, 1, 2, 6};  // D0, D1, D2, D6

// --- ShiftRegister74HC595 wiring (3 chips chained = 24 gate outputs)
//     Parameters: <number of chips> (data pin, clock pin, latch pin)
#define PIN_DATA   3     // D3 → 74HC595 pin 14 (DS)
#define PIN_CLK    4     // D4 → SH_CP
#define PIN_LATCH  5     // D5 → ST_CP
// Wire 74HC595 pin 13 (OE, active LOW) to GND for always-enabled outputs.
// Wire 74HC595 pin 10 (MR, active LOW) to VCC to prevent spurious clears.

// --- DAC full-scale constants
//     CC value 127 maps to this DAC code, giving the target max voltage.
//     DAC8568 with internal 2.5V ref × gain 2 → 5V full scale = 65535.
#define DAC_5V0  65535          // 127 → 5.000 V
#define DAC_3V3  43253          // 127 → 3.300 V  (65535 × 3.3 / 5.0)
#define DAC_2V0  26214          // 127 → 2.000 V  (65535 × 2.0 / 5.0)

// --- CC → DAC output mapping
//     Each entry: { CC number, full-scale DAC code }
//     Use ccNumber 255 to leave a DAC channel unassigned.
struct DacMapping {
  uint8_t  ccNumber;
  uint16_t fullScale;
};

static const DacMapping CC_TO_DAC[32] = {
  /* ch 0 */   { OSC1_SAW_LEVEL,    DAC_2V0 },
  /* ch 1 */   { OSC1_PULSE_LEVEL,  DAC_2V0 },
  /* ch 2 */   { OSC1_SUB_LEVEL,    DAC_2V0 },
  /* ch 3 */   { OSC2_SAW_LEVEL,    DAC_2V0 },
  /* ch 4 */   { OSC2_PULSE_LEVEL,  DAC_2V0 },
  /* ch 5 */   { OSC2_TRIANGLE_LEVEL,  DAC_2V0 },
  /* ch 6 */   { OSC1_PM_DCO,       DAC_2V0 },
  /* ch 7 */   { OSC1_PM_ENV,       DAC_2V0 },

  /* ch 8 */   { EG_DEPTH,          DAC_5V0 },
  /* ch 9 */   { 255,               DAC_5V0 },    
  /* ch 10 */  { 255,               DAC_5V0 },
  /* ch 11 */  { 255,               DAC_5V0 }, 
  /* ch 12 */  { 255,               DAC_5V0 },
  /* ch 13 */  { 255,               DAC_5V0 },    
  /* ch 14 */  { 255,               DAC_5V0 },
  /* ch 15 */  { 255,               DAC_5V0 }, 

  /* ch 16 */  { VCF_ATTACK,        DAC_5V0 },
  /* ch 17 */  { VCF_DECAY,         DAC_5V0 },
  /* ch 18 */  { VCF_SUSTAIN,       DAC_5V0 },
  /* ch 19 */  { VCF_RELEASE,       DAC_5V0 },
  /* ch 20 */  { VCA_ATTACK,        DAC_5V0 },
  /* ch 21 */  { VCA_DECAY,         DAC_5V0 },
  /* ch 22 */  { VCA_SUSTAIN,       DAC_5V0 },
  /* ch 23 */  { VCA_RELEASE,       DAC_5V0 },


  /* ch  24 */ { EFFECT_POT1,       DAC_3V3 },
  /* ch  25 */ { EFFECT_POT2,       DAC_3V3 },
  /* ch  26 */ { EFFECT_POT3,       DAC_3V3 },
  /* ch  27 */ { EFFECT_MIX,        DAC_2V0 },
  /* ch  28 */ { NOISE_LEVEL,       DAC_2V0 },
  /* ch  29 */ { FILTER_CUTOFF,     DAC_5V0 },
  /* ch  30 */ { FILTER_RES,        DAC_3V3 }, 
  /* ch  31 */ { VOLUME,            DAC_2V0 }

};

// --- CC → Gate (switch) output mapping
//     Index 0–23 = gate output 0–23
//     Value = CC number that drives that gate.
//     CC value 0 → LOW (0 V), CC value 127 → HIGH (5 V).
//     Values in between are treated as HIGH if >= GATE_THRESHOLD.
static const uint8_t CC_TO_GATE[24] = {
  FILTER_EG_INVERT, FILTER_VELOCITY, AMP_VELOCITY, FILTER_POLE, FILTER_LIN_LOG, AMP_LIN_LOG, POLYMOD_DEST_DCO1, POLYMOD_DEST_FILTER,
  EFFECT_BANK_1, EFFECT_BANK_2, EFFECT_BANK_3, EFFECT_2, EFFECT_1, EFFECT_0, EFFECT_INTERNAL, FILTER_PUNCH,  
  AMP_PUNCH, FILTER_MODE_BIT0, FILTER_MODE_BIT1, FILTER_A, FILTER_B, FILTER_C, AMP_MODE_BIT0, AMP_MODE_BIT1
};
static const uint8_t GATE_THRESHOLD = 64;   // CC >= this → gate HIGH

// ============================================================
//  DAC8568 driver
// ============================================================

// DAC8568 command bits (datasheet Table 1)
static const uint8_t DAC8568_CMD_WRITE_UPDATE = 0x03; // write & update channel

// Build a 32-bit SPI word for the DAC8568.
// channel: 0–7 (A–H)
// value:   0–65535 (16-bit)
static inline uint32_t dac8568Word(uint8_t channel, uint16_t value) {
  // Bits[31:28] = prefix 0000 (4-bit)
  // Bits[27:24] = command (WRITE_UPDATE = 0011)
  // Bits[23:20] = address (channel 0–7)
  // Bits[19: 4] = 16-bit data
  // Bits[ 3: 0] = feature bits (0 for normal operation)
  return ((uint32_t)DAC8568_CMD_WRITE_UPDATE << 24)
       | ((uint32_t)(channel & 0x07)         << 20)
       | ((uint32_t)(value)                  <<  4);
}

// Write one value to a DAC8568.
// dacIndex: 0–3 (which IC) m 
// channel:  0–7 (which channel on that IC)
// value:    0–65535
void dacWrite(uint8_t dacIndex, uint8_t channel, uint16_t value) {
  uint32_t word = dac8568Word(channel, value);

  digitalWrite(DAC_CS_PINS[dacIndex], LOW);
  SPI.transfer((word >> 24) & 0xFF);
  SPI.transfer((word >> 16) & 0xFF);
  SPI.transfer((word >>  8) & 0xFF);
  SPI.transfer( word        & 0xFF);
  digitalWrite(DAC_CS_PINS[dacIndex], HIGH);
}

// Set DAC channel by overall index (0–23) and a 16-bit value.
void dacSetChannel(uint8_t outputIndex, uint16_t value) {
  uint8_t dacIndex = outputIndex / 8;
  uint8_t channel  = outputIndex % 8;
  dacWrite(dacIndex, channel, value);
}

// Convert MIDI CC value (0–127) to a 16-bit DAC code.
// fullScale is the DAC code that corresponds to CC 127 on this channel.
static inline uint16_t ccToDacValue(uint8_t cc, uint16_t fullScale) {
  return (uint16_t)((uint32_t)cc * fullScale / 127UL);
}

// ============================================================
//  DAC8568 initialisation
// ============================================================

// DAC8568 internal reference control (command 0x08)
static const uint8_t DAC8568_CMD_INTREF = 0x08;

// Enable internal 2.5 V reference with ×2 gain → 5 V span.
// Set DAC_USE_INTERNAL_REF to false if you supply an external VREF.
static const bool DAC_USE_INTERNAL_REF = true;

void dacInit(uint8_t dacIndex) {
  if (DAC_USE_INTERNAL_REF) {
    // Feature byte 0x09: enable internal reference, gain = 2
    uint32_t word = ((uint32_t)DAC8568_CMD_INTREF << 24) | 0x000009UL;
    digitalWrite(DAC_CS_PINS[dacIndex], LOW);
    SPI.transfer((word >> 24) & 0xFF);
    SPI.transfer((word >> 16) & 0xFF);
    SPI.transfer((word >>  8) & 0xFF);
    SPI.transfer( word        & 0xFF);
    digitalWrite(DAC_CS_PINS[dacIndex], HIGH);
  }
  // Zero all channels on startup
  for (uint8_t ch = 0; ch < 8; ch++) {
    dacWrite(dacIndex, ch, 0);
  }
}

// ============================================================
//  Gate outputs — ShiftRegister74HC595
// ============================================================

// 3 chained 74HC595s = 24 outputs (pins 0–7 on chip 1, 8–15 on chip 2, 16–23 on chip 3)
ShiftRegister74HC595<3> sr(PIN_DATA, PIN_CLK, PIN_LATCH);

// Set one gate output HIGH or LOW.
// gateIndex: 0–15
// state:     true = HIGH (5 V), false = LOW (0 V)
void gateSetBit(uint8_t gateIndex, bool state) {
  sr.set(gateIndex, state ? HIGH : LOW);
}

// ============================================================
//  MIDI activity LED
// ============================================================

#define LED_ON_MS 20   // how long the LED stays on per MIDI message

static unsigned long ledOffTime = 0;

void ledUpdate() {
  if (ledOffTime && millis() >= ledOffTime) {
    digitalWrite(LED_BUILTIN, LOW);
    ledOffTime = 0;
  }
}

void ledFlash() {
  digitalWrite(LED_BUILTIN, HIGH);
  ledOffTime = millis() + LED_ON_MS;
}



MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

// ============================================================
//  CC lookup helpers
// ============================================================

// Find the DAC output index for a given CC number, or 255 if none.
uint8_t findDacForCC(uint8_t ccNumber) {
  for (uint8_t i = 0; i < 32; i++) {
    if (CC_TO_DAC[i].ccNumber == ccNumber) return i;
  }
  return 255;
}

// Find the gate output index for a given CC number, or 255 if none.
uint8_t findGateForCC(uint8_t ccNumber) {
  for (uint8_t i = 0; i < 24; i++) {
    if (CC_TO_GATE[i] == ccNumber) return i;
  }
  return 255;
}

// ============================================================
//  MIDI CC handler
// ============================================================

void handleControlChange(byte channel, byte ccNumber, byte ccValue) {
  ledFlash();

  // Filter by configured MIDI channel(s)
  bool channelMatch = (channel == MIDI_CHANNEL_A)
                   || (MIDI_CHANNEL_B != 0 && channel == MIDI_CHANNEL_B);
  if (!channelMatch) return;

  // --- Update DAC output if this CC is mapped
  uint8_t dacOut = findDacForCC(ccNumber);
  if (dacOut != 255) {
    dacSetChannel(dacOut, ccToDacValue(ccValue, CC_TO_DAC[dacOut].fullScale));
  }

  // --- Update gate output if this CC is mapped
  uint8_t gateOut = findGateForCC(ccNumber);
  if (gateOut != 255) {
    bool gateOn = (ccValue >= GATE_THRESHOLD);
    gateSetBit(gateOut, gateOn);
  }
}

// ============================================================
//  Arduino setup / loop
// ============================================================

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);



  // SPI
  SPI.begin();
  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE1));
  // DAC8568 uses SPI Mode 1 (CPOL=0, CPHA=1).
  // Some boards may need Mode 0 — check your DAC datasheet.

  // Chip-select pins — HIGH = deselected
  for (uint8_t i = 0; i < 4; i++) {
    pinMode(DAC_CS_PINS[i], OUTPUT);
    digitalWrite(DAC_CS_PINS[i], HIGH);
  }

  // Initialise all four DAC ICs
  for (uint8_t i = 0; i < 4; i++) {
    dacInit(i);
  }

  // All gates off on startup
  sr.setAllLow();

  // MIDI
  MIDI.begin(MIDI_CHANNEL_OMNI);   // receive all; we filter in the handler
  MIDI.setHandleControlChange(handleControlChange);

  pinMode(D6, OUTPUT);     // reclaim D6/TX as a digital pin
  digitalWrite(D6, HIGH);
}

void loop() {
  MIDI.read();
  ledUpdate();
}
