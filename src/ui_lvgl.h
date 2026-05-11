/**
 * ui_lvgl.h — LVGL UI for T-Deck Plus walkie-talkie
 *
 * Layout (320x240 landscape, no touch):
 *
 * ┌──────────────────────────────────────────────────────────┐
 * │ DUTY ███████████░░░░░░░░░░░░░░░░░ 24/36s  [1%]           │  16px duty cycle bar
 * ├──────────────────────────────────────────────────────────┤
 * │ 12:34:56  868.300  BAT 87%  BT●   SIG ▌▌▌▌▌              │  18px status
 * ├──────────────┬───────────────────────────────────────────┤
 * │              │ STANDBY                                   │
 * │   [PTT]      │ RSSI: -82 dBm   SNR: +4 dB                │
 * │   ROUND      │ Codec: 3200 bps                           │
 * │   BUTTON     │ Freq: 868.300 CH2                         │
 * │   80x80      │ Vol  ─────●──── 75                        │
 * │              │ Gate ──●──────  20  [open●]               │
 * ├──────────────┴───────────────────────────────────────────┤
 * │ GPS 51.50722N  0.12756W   LOCK   [MENU]  [KEYS]          │
 * └──────────────────────────────────────────────────────────┘
 *
 * Navigation:
 *   Trackball UP/DOWN/LEFT/RIGHT — move focus between widgets
 *   Trackball CLICK              — activate focused widget
 *   SPACE                        — PTT (always works, regardless of focus)
 *   ALT + M                      — open settings menu
 *   ALT + K                      — open key manager
 *   ALT + F                      — quick frequency cycle
 *   ALT + C                      — quick codec cycle
 */
#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "radio_config.h"
#include "duty_cycle.h"

// State queried by UI
struct UIState {
    int   freqIdx;
    int   codecIdx;
    int   volume;
    int   noiseGateThreshold;     // RMS units
    bool  noiseGateEnabled;
    bool  bluetoothEnabled;
    bool  btConnected;
    bool  gpsValid;
    float gpsLat, gpsLon;
    char  gpsTime[12];
    int   battPercent;
    int   rssi;
    float snr;
    bool  txActive;
    bool  rxActive;
    bool  noiseGateOpen;          // mic currently passing audio
    int   micRms;
    int   dutyUsedSec;
    int   dutyLimitSec;
    int   dutyPercent;
    bool  dutyBlocked;            // true when at cap
};

// UI callbacks back into main
struct UICallbacks {
    void (*onFreqChange)(int newIdx);
    void (*onCodecChange)(int newIdx);
    void (*onVolumeChange)(int pct);
    void (*onGateChange)(int rmsThreshold);
    void (*onGateToggle)(bool enabled);
    void (*onBtToggle)(bool enabled);
    void (*onPttRequest)(bool pressed);   // when trackball clicks PTT button
    void (*onKeyManagerOpen)();
    void (*onMenuOpen)();
};

class WalkieUI {
public:
    void begin(UICallbacks cb);
    void tick();                          // call from main loop
    void setState(const UIState &s);
    void feedKey(uint32_t lv_key);        // push translated key into LVGL keypad
    void flashTx(bool on);
    void showToast(const char *msg, uint32_t durationMs = 2000);

    // Settings dialog (modal)
    void openSettings();
    bool isSettingsOpen() const { return settingsModal_ != nullptr; }
    void closeSettings();

private:
    void buildMainScreen();
    void buildSettingsScreen();
    static void pttBtnEventCb(lv_event_t *e);
    static void volSliderEventCb(lv_event_t *e);
    static void gateSliderEventCb(lv_event_t *e);
    static void freqDropEventCb(lv_event_t *e);
    static void codecDropEventCb(lv_event_t *e);
    static void btSwitchEventCb(lv_event_t *e);
    static void keyMgrBtnEventCb(lv_event_t *e);
    static void menuBtnEventCb(lv_event_t *e);

    UICallbacks cb_{};
    UIState     lastState_{};

    // Widget handles
    lv_obj_t *screen_           = nullptr;
    lv_obj_t *dutyBar_          = nullptr;
    lv_obj_t *dutyLabel_        = nullptr;
    lv_obj_t *statusLine_       = nullptr;
    lv_obj_t *stateLabel_       = nullptr;
    lv_obj_t *rssiLabel_        = nullptr;
    lv_obj_t *codecLabel_       = nullptr;
    lv_obj_t *freqLabel_        = nullptr;
    lv_obj_t *gpsLabel_         = nullptr;
    lv_obj_t *pttBtn_           = nullptr;
    lv_obj_t *pttLabel_         = nullptr;
    lv_obj_t *volSlider_        = nullptr;
    lv_obj_t *volValue_         = nullptr;
    lv_obj_t *gateSlider_       = nullptr;
    lv_obj_t *gateValue_        = nullptr;
    lv_obj_t *gateOpenDot_      = nullptr;
    lv_obj_t *btIcon_           = nullptr;
    lv_obj_t *signalMeter_      = nullptr;
    lv_obj_t *settingsModal_    = nullptr;
    lv_obj_t *toastLabel_       = nullptr;
    lv_timer_t *toastTimer_     = nullptr;
    lv_group_t *navGroup_       = nullptr;

    static WalkieUI *instance_;  // for static event callbacks
};
