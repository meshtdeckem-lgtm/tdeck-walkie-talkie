/**
 * audio_dsp.h — Noise gate + simple voice DSP
 *
 * The noise gate computes RMS of each incoming PCM frame and compares
 * against a user-settable threshold. Frames below threshold are zeroed
 * (so silence is transmitted, saving airtime) or skipped entirely.
 *
 * Hysteresis prevents rapid open/close chatter: gate stays open for
 * holdMs after last above-threshold frame, then closes.
 */
#pragma once
#include <Arduino.h>
#include <math.h>

class NoiseGate {
public:
    NoiseGate() :
        thresholdRms(800),     // ~ -32 dBFS, picks up normal speech
        holdMs(250),
        attackMs(5),
        gateOpen(false),
        lastAboveMs(0),
        gainRamp(0.0f),
        enabled(true) {}

    void setThreshold(int rmsThreshold) { thresholdRms = rmsThreshold; }
    int  getThreshold() const           { return thresholdRms; }
    void setEnabled(bool en)            { enabled = en; }
    bool isEnabled() const              { return enabled; }
    bool isOpen() const                 { return gateOpen; }

    // Returns the current RMS of the frame for UI metering
    int processFrame(int16_t *pcm, int samples) {
        // Compute RMS
        int64_t sumSq = 0;
        for (int i = 0; i < samples; i++) sumSq += (int32_t)pcm[i] * pcm[i];
        int rms = (int)sqrt((double)sumSq / samples);

        if (!enabled) {
            gateOpen = true;
            gainRamp = 1.0f;
            return rms;
        }

        // Gate logic
        if (rms >= thresholdRms) {
            lastAboveMs = millis();
            gateOpen = true;
        } else if (gateOpen && (millis() - lastAboveMs > (unsigned long)holdMs)) {
            gateOpen = false;
        }

        // Apply smooth gain ramp (avoid clicks)
        float target = gateOpen ? 1.0f : 0.0f;
        float step = 1.0f / max(1, (attackMs * 8));  // 8 samples per ms @ 8kHz
        for (int i = 0; i < samples; i++) {
            if (gainRamp < target)      gainRamp = min(target, gainRamp + step);
            else if (gainRamp > target) gainRamp = max(target, gainRamp - step);
            pcm[i] = (int16_t)(pcm[i] * gainRamp);
        }

        return rms;
    }

private:
    int   thresholdRms;
    int   holdMs;
    int   attackMs;
    bool  gateOpen;
    unsigned long lastAboveMs;
    float gainRamp;
    bool  enabled;
};

// ─── Software volume control (output side) ───────────────────────────────────
class VolumeControl {
public:
    VolumeControl() : levelPercent(75) {}
    void setLevel(int pct) { levelPercent = constrain(pct, 0, 100); }
    int  getLevel() const  { return levelPercent; }

    void apply(int16_t *pcm, int samples) {
        // Logarithmic volume curve feels more natural than linear
        float gain = (levelPercent == 0) ? 0.0f
                    : powf(10.0f, ((levelPercent - 100) * 0.025f));  // 0dB at 100%, -50dB at 0%
        for (int i = 0; i < samples; i++) {
            int32_t s = (int32_t)(pcm[i] * gain);
            pcm[i] = (int16_t)constrain(s, -32768, 32767);
        }
    }

private:
    int levelPercent;
};
