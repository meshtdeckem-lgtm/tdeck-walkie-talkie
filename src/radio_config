/**
 * radio_config.h — Radio frequencies & Codec2 quality presets
 */
#pragma once
#include <Arduino.h>
#include "codec2.h"

// ─── EU 868 MHz channels ─────────────────────────────────────────────────────
// All in sub-band g1 (868.0–868.6 MHz, 1% duty cycle, 14 dBm max ERP)
// 25 kHz spacing
struct FreqPreset {
    const char *label;
    float       freqMHz;
    uint8_t     dutyCyclePercent;   // legal limit on this sub-band
    int8_t      maxPowerDbm;
};

static const FreqPreset FREQ_PRESETS[] = {
    {"868.100 CH1", 868.100f, 1,  14},
    {"868.300 CH2", 868.300f, 1,  14},
    {"868.500 CH3", 868.500f, 1,  14},
    {"869.525 H10", 869.525f, 10, 27},   // g3 high-power sub-band (10% duty)
    {"869.700 CH4", 869.700f, 1,  14},
};
static const int FREQ_PRESET_COUNT = sizeof(FREQ_PRESETS) / sizeof(FREQ_PRESETS[0]);

// ─── Codec2 quality presets ───────────────────────────────────────────────────
// Tradeoff: lower bitrate = less airtime = more talk time under duty cycle,
//           but progressively worse audio.
struct CodecPreset {
    const char *label;
    int   codec2Mode;          // CODEC2_MODE_*
    int   samplesPerFrame;     // PCM samples per encoded frame
    int   bytesPerFrame;       // bytes per encoded frame
    int   frameMs;             // frame duration in ms
};

static const CodecPreset CODEC_PRESETS[] = {
    {"3200 bps (best)",  CODEC2_MODE_3200, 160, 8, 20},
    {"2400 bps",         CODEC2_MODE_2400, 160, 6, 20},
    {"1600 bps",         CODEC2_MODE_1600, 320, 8, 40},
    {"1200 bps",         CODEC2_MODE_1200, 320, 6, 40},
    {"700C bps (range)", CODEC2_MODE_700C, 320, 4, 40},
};
static const int CODEC_PRESET_COUNT = sizeof(CODEC_PRESETS) / sizeof(CODEC_PRESETS[0]);

// ─── FSK params (shared across all freq presets) ─────────────────────────────
#define FSK_BITRATE       4.8f
#define FSK_FREQ_DEV      5.0f
#define FSK_RX_BW         12.5f
#define FSK_PREAMBLE      8
