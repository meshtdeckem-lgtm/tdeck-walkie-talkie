#include "ui_lvgl.h"

WalkieUI *WalkieUI::instance_ = nullptr;

// ─── Color palette (tactical dark) ───────────────────────────────────────────
#define COL_BG          lv_color_hex(0x000000)
#define COL_PANEL       lv_color_hex(0x101010)
#define COL_BORDER      lv_color_hex(0x294529)
#define COL_ACCENT      lv_color_hex(0x00FF55)
#define COL_DIM         lv_color_hex(0x606060)
#define COL_TEXT        lv_color_hex(0xE0E0E0)
#define COL_TX          lv_color_hex(0xFF2020)
#define COL_RX          lv_color_hex(0x2080FF)
#define COL_WARN        lv_color_hex(0xFFC020)

// ─── PTT button event ─────────────────────────────────────────────────────────
void WalkieUI::pttBtnEventCb(lv_event_t *e) {
    auto code = lv_event_get_code(e);
    if (!instance_ || !instance_->cb_.onPttRequest) return;
    if (code == LV_EVENT_PRESSED)  instance_->cb_.onPttRequest(true);
    if (code == LV_EVENT_RELEASED) instance_->cb_.onPttRequest(false);
}

void WalkieUI::volSliderEventCb(lv_event_t *e) {
    if (!instance_ || !instance_->cb_.onVolumeChange) return;
    int v = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    lv_label_set_text_fmt(instance_->volValue_, "%d", v);
    instance_->cb_.onVolumeChange(v);
}

void WalkieUI::gateSliderEventCb(lv_event_t *e) {
    if (!instance_ || !instance_->cb_.onGateChange) return;
    int v = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    // Map 0-100 slider to RMS 0-3000
    int rms = v * 30;
    lv_label_set_text_fmt(instance_->gateValue_, "%d", v);
    instance_->cb_.onGateChange(rms);
}

void WalkieUI::freqDropEventCb(lv_event_t *e) {
    if (!instance_ || !instance_->cb_.onFreqChange) return;
    int sel = lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e));
    instance_->cb_.onFreqChange(sel);
}

void WalkieUI::codecDropEventCb(lv_event_t *e) {
    if (!instance_ || !instance_->cb_.onCodecChange) return;
    int sel = lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e));
    instance_->cb_.onCodecChange(sel);
}

void WalkieUI::btSwitchEventCb(lv_event_t *e) {
    if (!instance_ || !instance_->cb_.onBtToggle) return;
    bool on = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
    instance_->cb_.onBtToggle(on);
}

void WalkieUI::keyMgrBtnEventCb(lv_event_t *e) {
    if (!instance_ || !instance_->cb_.onKeyManagerOpen) return;
    instance_->cb_.onKeyManagerOpen();
}

void WalkieUI::menuBtnEventCb(lv_event_t *e) {
    if (!instance_) return;
    instance_->openSettings();
}

// ─── Build main screen ───────────────────────────────────────────────────────
void WalkieUI::buildMainScreen() {
    screen_ = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_, COL_BG, 0);
    lv_obj_set_style_pad_all(screen_, 0, 0);
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

    // Nav group for keyboard / trackball
    navGroup_ = lv_group_create();
    lv_group_set_default(navGroup_);

    // ── Top: Duty cycle bar (320 x 18) ──
    dutyBar_ = lv_bar_create(screen_);
    lv_obj_set_size(dutyBar_, 240, 14);
    lv_obj_align(dutyBar_, LV_ALIGN_TOP_LEFT, 4, 3);
    lv_bar_set_range(dutyBar_, 0, 100);
    lv_obj_set_style_bg_color(dutyBar_, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_color(dutyBar_, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_border_color(dutyBar_, COL_BORDER, 0);
    lv_obj_set_style_border_width(dutyBar_, 1, 0);

    dutyLabel_ = lv_label_create(screen_);
    lv_obj_align(dutyLabel_, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_text_color(dutyLabel_, COL_TEXT, 0);
    lv_obj_set_style_text_font(dutyLabel_, &lv_font_montserrat_12, 0);
    lv_label_set_text(dutyLabel_, "0/36s");

    // ── Status line (320 x 18 @ y=22) ──
    statusLine_ = lv_label_create(screen_);
    lv_obj_align(statusLine_, LV_ALIGN_TOP_LEFT, 4, 22);
    lv_obj_set_style_text_color(statusLine_, COL_TEXT, 0);
    lv_obj_set_style_text_font(statusLine_, &lv_font_montserrat_12, 0);
    lv_label_set_text(statusLine_, "--:--:--   ---.--- MHz   BAT --%");

    // BT icon (right side of status line)
    btIcon_ = lv_label_create(screen_);
    lv_obj_align(btIcon_, LV_ALIGN_TOP_RIGHT, -52, 22);
    lv_obj_set_style_text_color(btIcon_, COL_DIM, 0);
    lv_label_set_text(btIcon_, LV_SYMBOL_BLUETOOTH);

    // Signal meter (right side)
    signalMeter_ = lv_bar_create(screen_);
    lv_obj_set_size(signalMeter_, 40, 10);
    lv_obj_align(signalMeter_, LV_ALIGN_TOP_RIGHT, -4, 24);
    lv_bar_set_range(signalMeter_, 0, 5);
    lv_obj_set_style_bg_color(signalMeter_, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_color(signalMeter_, COL_ACCENT, LV_PART_INDICATOR);

    // ── PTT button (left column, 90x90 @ y=46) ──
    pttBtn_ = lv_button_create(screen_);
    lv_obj_set_size(pttBtn_, 90, 90);
    lv_obj_align(pttBtn_, LV_ALIGN_TOP_LEFT, 6, 46);
    lv_obj_set_style_radius(pttBtn_, 45, 0);
    lv_obj_set_style_bg_color(pttBtn_, lv_color_hex(0x303030), 0);
    lv_obj_set_style_bg_color(pttBtn_, COL_TX, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(pttBtn_, COL_BORDER, 0);
    lv_obj_set_style_border_width(pttBtn_, 2, 0);
    lv_obj_set_style_border_color(pttBtn_, COL_TX, LV_STATE_PRESSED);
    lv_obj_add_event_cb(pttBtn_, pttBtnEventCb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(pttBtn_, pttBtnEventCb, LV_EVENT_RELEASED, NULL);
    lv_group_add_obj(navGroup_, pttBtn_);

    pttLabel_ = lv_label_create(pttBtn_);
    lv_label_set_text(pttLabel_, "PTT");
    lv_obj_set_style_text_font(pttLabel_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(pttLabel_, COL_TEXT, 0);
    lv_obj_center(pttLabel_);

    // ── Right column: state info + sliders (y=46 onwards, x=110+) ──
    stateLabel_ = lv_label_create(screen_);
    lv_obj_align(stateLabel_, LV_ALIGN_TOP_LEFT, 110, 46);
    lv_obj_set_style_text_font(stateLabel_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(stateLabel_, COL_ACCENT, 0);
    lv_label_set_text(stateLabel_, "STANDBY");

    rssiLabel_ = lv_label_create(screen_);
    lv_obj_align(rssiLabel_, LV_ALIGN_TOP_LEFT, 110, 70);
    lv_obj_set_style_text_font(rssiLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(rssiLabel_, COL_TEXT, 0);
    lv_label_set_text(rssiLabel_, "RSSI: ---  SNR: ---");

    codecLabel_ = lv_label_create(screen_);
    lv_obj_align(codecLabel_, LV_ALIGN_TOP_LEFT, 110, 86);
    lv_obj_set_style_text_font(codecLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(codecLabel_, COL_TEXT, 0);
    lv_label_set_text(codecLabel_, "Codec: 3200 bps");

    freqLabel_ = lv_label_create(screen_);
    lv_obj_align(freqLabel_, LV_ALIGN_TOP_LEFT, 110, 100);
    lv_obj_set_style_text_font(freqLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(freqLabel_, COL_TEXT, 0);
    lv_label_set_text(freqLabel_, "Freq:  868.100 CH1");

    // Volume slider
    lv_obj_t *volLbl = lv_label_create(screen_);
    lv_obj_align(volLbl, LV_ALIGN_TOP_LEFT, 110, 118);
    lv_obj_set_style_text_font(volLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(volLbl, COL_DIM, 0);
    lv_label_set_text(volLbl, "VOL");

    volSlider_ = lv_slider_create(screen_);
    lv_obj_set_size(volSlider_, 130, 10);
    lv_obj_align(volSlider_, LV_ALIGN_TOP_LEFT, 138, 122);
    lv_slider_set_range(volSlider_, 0, 100);
    lv_slider_set_value(volSlider_, 75, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(volSlider_, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_color(volSlider_, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volSlider_, COL_ACCENT, LV_PART_KNOB);
    lv_obj_add_event_cb(volSlider_, volSliderEventCb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_group_add_obj(navGroup_, volSlider_);

    volValue_ = lv_label_create(screen_);
    lv_obj_align(volValue_, LV_ALIGN_TOP_LEFT, 275, 118);
    lv_obj_set_style_text_font(volValue_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(volValue_, COL_TEXT, 0);
    lv_label_set_text(volValue_, "75");

    // Noise gate slider
    lv_obj_t *gateLbl = lv_label_create(screen_);
    lv_obj_align(gateLbl, LV_ALIGN_TOP_LEFT, 110, 138);
    lv_obj_set_style_text_font(gateLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(gateLbl, COL_DIM, 0);
    lv_label_set_text(gateLbl, "GATE");

    gateSlider_ = lv_slider_create(screen_);
    lv_obj_set_size(gateSlider_, 110, 10);
    lv_obj_align(gateSlider_, LV_ALIGN_TOP_LEFT, 148, 142);
    lv_slider_set_range(gateSlider_, 0, 100);
    lv_slider_set_value(gateSlider_, 27, LV_ANIM_OFF);   // ~800 RMS
    lv_obj_set_style_bg_color(gateSlider_, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_color(gateSlider_, COL_WARN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(gateSlider_, COL_WARN, LV_PART_KNOB);
    lv_obj_add_event_cb(gateSlider_, gateSliderEventCb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_group_add_obj(navGroup_, gateSlider_);

    gateValue_ = lv_label_create(screen_);
    lv_obj_align(gateValue_, LV_ALIGN_TOP_LEFT, 263, 138);
    lv_obj_set_style_text_font(gateValue_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(gateValue_, COL_TEXT, 0);
    lv_label_set_text(gateValue_, "27");

    gateOpenDot_ = lv_obj_create(screen_);
    lv_obj_set_size(gateOpenDot_, 10, 10);
    lv_obj_align(gateOpenDot_, LV_ALIGN_TOP_LEFT, 290, 140);
    lv_obj_set_style_radius(gateOpenDot_, 5, 0);
    lv_obj_set_style_bg_color(gateOpenDot_, COL_DIM, 0);
    lv_obj_set_style_border_width(gateOpenDot_, 0, 0);

    // ── Bottom: GPS + buttons (y=158 onwards) ──
    gpsLabel_ = lv_label_create(screen_);
    lv_obj_align(gpsLabel_, LV_ALIGN_BOTTOM_LEFT, 6, -28);
    lv_obj_set_style_text_font(gpsLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(gpsLabel_, COL_DIM, 0);
    lv_label_set_text(gpsLabel_, "GPS  acquiring...");

    // Menu and Keys buttons
    lv_obj_t *menuBtn = lv_button_create(screen_);
    lv_obj_set_size(menuBtn, 60, 22);
    lv_obj_align(menuBtn, LV_ALIGN_BOTTOM_RIGHT, -68, -4);
    lv_obj_set_style_bg_color(menuBtn, lv_color_hex(0x202040), 0);
    lv_obj_set_style_border_color(menuBtn, COL_BORDER, 0);
    lv_obj_set_style_border_width(menuBtn, 1, 0);
    lv_obj_add_event_cb(menuBtn, menuBtnEventCb, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(navGroup_, menuBtn);
    lv_obj_t *menuLbl = lv_label_create(menuBtn);
    lv_label_set_text(menuLbl, "MENU");
    lv_obj_set_style_text_font(menuLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(menuLbl);

    lv_obj_t *keyBtn = lv_button_create(screen_);
    lv_obj_set_size(keyBtn, 60, 22);
    lv_obj_align(keyBtn, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    lv_obj_set_style_bg_color(keyBtn, lv_color_hex(0x402020), 0);
    lv_obj_set_style_border_color(keyBtn, COL_BORDER, 0);
    lv_obj_set_style_border_width(keyBtn, 1, 0);
    lv_obj_add_event_cb(keyBtn, keyMgrBtnEventCb, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(navGroup_, keyBtn);
    lv_obj_t *keyLbl = lv_label_create(keyBtn);
    lv_label_set_text(keyLbl, "KEYS");
    lv_obj_set_style_text_font(keyLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(keyLbl);

    lv_scr_load(screen_);
}

// ─── Settings modal ──────────────────────────────────────────────────────────
void WalkieUI::openSettings() {
    if (settingsModal_) return;

    settingsModal_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(settingsModal_, 300, 220);
    lv_obj_center(settingsModal_);
    lv_obj_set_style_bg_color(settingsModal_, lv_color_hex(0x101820), 0);
    lv_obj_set_style_border_color(settingsModal_, COL_ACCENT, 0);
    lv_obj_set_style_border_width(settingsModal_, 2, 0);
    lv_obj_set_style_pad_all(settingsModal_, 8, 0);

    lv_obj_t *title = lv_label_create(settingsModal_);
    lv_label_set_text(title, "SETTINGS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COL_ACCENT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    // Frequency dropdown
    lv_obj_t *fLbl = lv_label_create(settingsModal_);
    lv_label_set_text(fLbl, "Frequency");
    lv_obj_align(fLbl, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_obj_set_style_text_font(fLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(fLbl, COL_TEXT, 0);

    lv_obj_t *fDrop = lv_dropdown_create(settingsModal_);
    char fOpts[256] = "";
    for (int i = 0; i < FREQ_PRESET_COUNT; i++) {
        strcat(fOpts, FREQ_PRESETS[i].label);
        if (i < FREQ_PRESET_COUNT - 1) strcat(fOpts, "\n");
    }
    lv_dropdown_set_options(fDrop, fOpts);
    lv_dropdown_set_selected(fDrop, lastState_.freqIdx);
    lv_obj_set_width(fDrop, 160);
    lv_obj_align(fDrop, LV_ALIGN_TOP_LEFT, 110, 26);
    lv_obj_add_event_cb(fDrop, freqDropEventCb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_group_add_obj(navGroup_, fDrop);

    // Codec dropdown
    lv_obj_t *cLbl = lv_label_create(settingsModal_);
    lv_label_set_text(cLbl, "Codec");
    lv_obj_align(cLbl, LV_ALIGN_TOP_LEFT, 0, 66);
    lv_obj_set_style_text_font(cLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cLbl, COL_TEXT, 0);

    lv_obj_t *cDrop = lv_dropdown_create(settingsModal_);
    char cOpts[256] = "";
    for (int i = 0; i < CODEC_PRESET_COUNT; i++) {
        strcat(cOpts, CODEC_PRESETS[i].label);
        if (i < CODEC_PRESET_COUNT - 1) strcat(cOpts, "\n");
    }
    lv_dropdown_set_options(cDrop, cOpts);
    lv_dropdown_set_selected(cDrop, lastState_.codecIdx);
    lv_obj_set_width(cDrop, 160);
    lv_obj_align(cDrop, LV_ALIGN_TOP_LEFT, 110, 62);
    lv_obj_add_event_cb(cDrop, codecDropEventCb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_group_add_obj(navGroup_, cDrop);

    // Bluetooth toggle
    lv_obj_t *btLbl = lv_label_create(settingsModal_);
    lv_label_set_text(btLbl, "Bluetooth");
    lv_obj_align(btLbl, LV_ALIGN_TOP_LEFT, 0, 104);
    lv_obj_set_style_text_font(btLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(btLbl, COL_TEXT, 0);

    lv_obj_t *btSw = lv_switch_create(settingsModal_);
    lv_obj_align(btSw, LV_ALIGN_TOP_LEFT, 110, 100);
    if (lastState_.bluetoothEnabled) lv_obj_add_state(btSw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(btSw, btSwitchEventCb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_group_add_obj(navGroup_, btSw);

    lv_obj_t *btNote = lv_label_create(settingsModal_);
    lv_label_set_text(btNote, "BLE config service\n(no audio - S3 lacks A2DP)");
    lv_obj_align(btNote, LV_ALIGN_TOP_LEFT, 170, 96);
    lv_obj_set_style_text_font(btNote, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(btNote, COL_DIM, 0);

    // Close button
    lv_obj_t *closeBtn = lv_button_create(settingsModal_);
    lv_obj_set_size(closeBtn, 80, 30);
    lv_obj_align(closeBtn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t *closeLbl = lv_label_create(closeBtn);
    lv_label_set_text(closeLbl, "CLOSE");
    lv_obj_center(closeLbl);
    lv_obj_add_event_cb(closeBtn, [](lv_event_t *e) {
        if (instance_) instance_->closeSettings();
    }, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(navGroup_, closeBtn);
    lv_group_focus_obj(closeBtn);
}

void WalkieUI::closeSettings() {
    if (settingsModal_) {
        lv_obj_del(settingsModal_);
        settingsModal_ = nullptr;
    }
}

// ─── begin() ──────────────────────────────────────────────────────────────────
void WalkieUI::begin(UICallbacks cb) {
    cb_ = cb;
    instance_ = this;
    buildMainScreen();
}

// ─── tick() — refresh dynamic widgets from lastState_ ────────────────────────
void WalkieUI::tick() {
    lv_timer_handler();
}

// ─── setState — push state into widgets ──────────────────────────────────────
void WalkieUI::setState(const UIState &s) {
    lastState_ = s;
    if (!screen_) return;

    // Duty cycle bar
    int dutyPct = s.dutyPercent;
    lv_bar_set_value(dutyBar_, dutyPct, LV_ANIM_ON);
    lv_color_t dColor = COL_ACCENT;
    if (dutyPct > 80)      dColor = COL_TX;
    else if (dutyPct > 50) dColor = COL_WARN;
    lv_obj_set_style_bg_color(dutyBar_, dColor, LV_PART_INDICATOR);
    lv_label_set_text_fmt(dutyLabel_, "%d/%ds", s.dutyUsedSec, s.dutyLimitSec);

    // Status line
    const char *fLbl = FREQ_PRESETS[s.freqIdx].label;
    lv_label_set_text_fmt(statusLine_, "%s   %s   BAT %d%%",
                          s.gpsTime, fLbl, s.battPercent);

    // BT icon color
    lv_obj_set_style_text_color(btIcon_,
        s.btConnected ? COL_RX : (s.bluetoothEnabled ? COL_ACCENT : COL_DIM), 0);

    // Signal meter (RSSI mapped to 0..5 bars)
    int bars = 0;
    if (s.rssi > -70)       bars = 5;
    else if (s.rssi > -85)  bars = 4;
    else if (s.rssi > -95)  bars = 3;
    else if (s.rssi > -105) bars = 2;
    else if (s.rssi > -115) bars = 1;
    lv_bar_set_value(signalMeter_, bars, LV_ANIM_OFF);

    // State label + color
    if (s.dutyBlocked) {
        lv_label_set_text(stateLabel_, "BLOCKED");
        lv_obj_set_style_text_color(stateLabel_, COL_WARN, 0);
    } else if (s.txActive) {
        lv_label_set_text(stateLabel_, "TRANSMIT");
        lv_obj_set_style_text_color(stateLabel_, COL_TX, 0);
    } else if (s.rxActive) {
        lv_label_set_text(stateLabel_, "RECEIVE");
        lv_obj_set_style_text_color(stateLabel_, COL_RX, 0);
    } else {
        lv_label_set_text(stateLabel_, "STANDBY");
        lv_obj_set_style_text_color(stateLabel_, COL_ACCENT, 0);
    }

    // PTT button visual state
    if (s.txActive) {
        lv_obj_set_style_bg_color(pttBtn_, COL_TX, 0);
        lv_obj_set_style_border_color(pttBtn_, COL_TX, 0);
        lv_label_set_text(pttLabel_, "TX");
    } else {
        lv_obj_set_style_bg_color(pttBtn_, lv_color_hex(0x303030), 0);
        lv_obj_set_style_border_color(pttBtn_, COL_BORDER, 0);
        lv_label_set_text(pttLabel_, "PTT");
    }

    // RSSI/SNR
    lv_label_set_text_fmt(rssiLabel_, "RSSI: %d dBm  SNR: %+.1f dB", s.rssi, s.snr);

    // Codec / freq labels
    lv_label_set_text_fmt(codecLabel_, "Codec: %s", CODEC_PRESETS[s.codecIdx].label);
    lv_label_set_text_fmt(freqLabel_,  "Freq:  %s", FREQ_PRESETS[s.freqIdx].label);

    // GPS
    if (s.gpsValid) {
        lv_label_set_text_fmt(gpsLabel_, "GPS %.5f%c  %.5f%c  LOCK",
                              fabs(s.gpsLat), s.gpsLat >= 0 ? 'N' : 'S',
                              fabs(s.gpsLon), s.gpsLon >= 0 ? 'E' : 'W');
        lv_obj_set_style_text_color(gpsLabel_, COL_ACCENT, 0);
    } else {
        lv_label_set_text(gpsLabel_, "GPS  acquiring fix...");
        lv_obj_set_style_text_color(gpsLabel_, COL_DIM, 0);
    }

    // Gate open indicator
    lv_obj_set_style_bg_color(gateOpenDot_,
        s.noiseGateOpen ? COL_ACCENT : COL_DIM, 0);
}

// Forward keypad events into LVGL
void WalkieUI::feedKey(uint32_t lv_key) {
    // Caller is responsible for keypad indev driver — this is just a hook
    // for any custom shortcut handling beyond LVGL focus nav.
}

void WalkieUI::flashTx(bool on) {
    // Could blink the PTT button — handled via setState's txActive now
}

void WalkieUI::showToast(const char *msg, uint32_t durationMs) {
    if (toastLabel_) {
        lv_obj_del(toastLabel_);
        toastLabel_ = nullptr;
    }
    if (toastTimer_) {
        lv_timer_del(toastTimer_);
        toastTimer_ = nullptr;
    }
    toastLabel_ = lv_label_create(lv_scr_act());
    lv_label_set_text(toastLabel_, msg);
    lv_obj_set_style_bg_color(toastLabel_, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(toastLabel_, LV_OPA_90, 0);
    lv_obj_set_style_text_color(toastLabel_, COL_ACCENT, 0);
    lv_obj_set_style_pad_all(toastLabel_, 6, 0);
    lv_obj_set_style_border_color(toastLabel_, COL_ACCENT, 0);
    lv_obj_set_style_border_width(toastLabel_, 1, 0);
    lv_obj_align(toastLabel_, LV_ALIGN_CENTER, 0, 60);

    toastTimer_ = lv_timer_create([](lv_timer_t *t) {
        WalkieUI *self = (WalkieUI*)lv_timer_get_user_data(t);
        if (self && self->toastLabel_) {
            lv_obj_del(self->toastLabel_);
            self->toastLabel_ = nullptr;
        }
        self->toastTimer_ = nullptr;
        lv_timer_del(t);
    }, durationMs, this);
}
