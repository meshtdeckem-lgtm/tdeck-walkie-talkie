/**
 * duty_cycle.h — Rolling-window TX duty cycle tracker (EU 868 compliance)
 *
 * Tracks cumulative TX time over the last 3600 seconds (1 hour).
 * Limit at 1% = 36 seconds per hour, 10% = 360 seconds (g3 sub-band).
 *
 * Storage: ring buffer of 60 buckets, each = 60 seconds.
 * On every TX, addTxTime(ms) is called. getUsedSeconds() returns total
 * TX time in the rolling window. canTransmit() enforces the cap.
 */
#pragma once
#include <Arduino.h>

class DutyCycleTracker {
public:
    static const int NUM_BUCKETS    = 60;        // 60 buckets x 60s = 1 hour
    static const int BUCKET_MS      = 60 * 1000;
    static const int WINDOW_MS      = NUM_BUCKETS * BUCKET_MS;

    DutyCycleTracker() { reset(); }

    void reset() {
        memset(buckets, 0, sizeof(buckets));
        lastTickMs = millis();
        currentBucket = 0;
        limitSec = 36;   // default to 1% EU limit
    }

    void setLimitSeconds(int sec) { limitSec = sec; }
    int  getLimitSeconds() const  { return limitSec; }

    // Call every loop to age out old buckets
    void update() {
        unsigned long now = millis();
        while (now - lastTickMs >= (unsigned long)BUCKET_MS) {
            currentBucket = (currentBucket + 1) % NUM_BUCKETS;
            buckets[currentBucket] = 0;  // clear new current
            lastTickMs += BUCKET_MS;
        }
    }

    // Record TX time in milliseconds
    void addTxTime(uint32_t txMs) {
        update();
        buckets[currentBucket] += txMs;
    }

    // Total TX time across the rolling window, in seconds
    uint32_t getUsedSeconds() {
        update();
        uint32_t totalMs = 0;
        for (int i = 0; i < NUM_BUCKETS; i++) totalMs += buckets[i];
        return totalMs / 1000;
    }

    // 0–100: percent of the legal limit consumed
    int getUsedPercent() {
        uint32_t used = getUsedSeconds();
        int pct = (int)((used * 100UL) / (uint32_t)limitSec);
        return constrain(pct, 0, 100);
    }

    bool canTransmit() {
        return getUsedSeconds() < (uint32_t)limitSec;
    }

    // Seconds remaining before duty-cycle resets enough to allow more TX
    int getCooldownSeconds() {
        if (canTransmit()) return 0;
        // Find oldest bucket with TX time, return seconds until it ages out
        for (int i = 1; i <= NUM_BUCKETS; i++) {
            int idx = (currentBucket + i) % NUM_BUCKETS;
            if (buckets[idx] > 0) {
                unsigned long elapsedInCurrent = millis() - lastTickMs;
                return (NUM_BUCKETS - i) * 60 + (60 - elapsedInCurrent / 1000);
            }
        }
        return 0;
    }

private:
    uint32_t       buckets[NUM_BUCKETS];  // TX milliseconds per bucket
    int            currentBucket;
    unsigned long  lastTickMs;
    int            limitSec;
};
