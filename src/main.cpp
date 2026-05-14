/**
 * tdeck_walkie_talkie.ino — T-Deck Plus LoRa Walkie-Talkie (LVGL build)
 *
 * Features:
 *   • LVGL 9.x UI with on-screen PTT, sliders, dropdowns
 *   • Selectable Codec2 quality (700/1200/1600/2400/3200 bps)
 *   • Noise gate with adjustable RMS threshold
 *   • On-screen volume control (software gain on RX audio)
 *   • Frequency changeover (5 EU 868 channels including high-power g3)
 *   • EU duty cycle bar — 36s/hour on 1% channels, 360s on g3
 *   • AES-256-GCM encryption (per-packet IV, authenticated)
 *   • BLE GATT service for remote config + secure key exchange
 *   • GPS display, battery, signal meter
 *   • Key manager (ALT+K) — generate / type / pair via LoRa / BLE
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <RadioLib.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <Preferences.h>
#include "mbedtls/gcm.h"
#include "driver/i2s.h"
#include "esp_random.h"
#include "codec2.h"
#include "secrets.h"
#include "radio_config.h"
#include "duty_cycle.h"
#include "audio_dsp.h"
#include "ui_lvgl.h"
#include "bluetooth_mgr.h"

// ════════════════════════════════════════════════════════════════════════════
//                            HARDWARE PIN MAP
// ════════════════════════════════════════════════════════════════════════════
#define BOARD_POWERON       10
#define BOARD_TFT_CS        12
#define BOARD_TFT_DC        11
#define BOARD_TFT_BACKLIGHT 42
#define BOARD_SPI_MOSI      41
#define BOARD_SPI_MISO      38
#define BOARD_SPI_SCK       40

#define RADIO_CS_PIN        9
#define RADIO_RST_PIN       17
#define RADIO_DIO1_PIN      45
#define RADIO_BUSY_PIN      13

#define ES7210_MCLK         48
#define ES7210_LRCK         21
#define ES7210_SCK          47
#define ES7210_DIN          14
#define ES7210_I2C_ADDR     0x40

#define I2S_OUT_WS          5
#define I2S_OUT_BCK         7
#define I2S_OUT_DATA        6

#define I2C_SDA             18
#define I2C_SCL             8
#define KB_INT              46
#define KB_I2C_ADDR         0x55

#define GPS_TX_PIN          43
#define GPS_RX_PIN          44

#define TRACKBALL_UP        3
#define TRACKBALL_DN        15
#define TRACKBALL_LT        1
#define TRACKBALL_RT        2
#define TRACKBALL_CLICK     0   // BOOT button shared

#define BAT_ADC             4

// ════════════════════════════════════════════════════════════════════════════
//                            GLOBALS
// ════════════════════════════════════════════════════════════════════════════
SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
TFT_eSPI tft = TFT_eSPI();
WalkieUI ui;
DutyCycleTracker duty;
NoiseGate gate;
VolumeControl vol;
BluetoothMgr btMgr;

// AES key — seeded from secrets.h in setup(), overridden by NVS if available
uint8_t AES_KEY[32];

#define AES_IV_LEN  12
#define AES_TAG_LEN 16
#define PKT_MAGIC   0xA5

// Codec / radio state
struct CODEC2 *c2 = nullptr;
int currentCodecIdx = 0;
int currentFreqIdx  = 0;

// Audio buffers (sized for largest codec frame combo)
#define MAX_PCM_PER_PACKET   (320 * 2)  // up to 80ms @ 8kHz
#define MAX_BYTES_PER_PACKET 16
static int16_t micBuf[MAX_PCM_PER_PACKET];
static int16_t spkBuf[MAX_PCM_PER_PACKET];
static uint8_t codec2Buf[MAX_BYTES_PER_PACKET];

// State
volatile bool pttHardware = false;  // spacebar or trackball-click
volatile bool pttUI       = false;  // LVGL on-screen button press
bool txActive = false;
bool rxActive = false;

// GPS
static char gpsBuf[128];
static int gpsBufIdx = 0;
static float gpsLat = 0, gpsLon = 0;
static bool  gpsValid = false;
static char  gpsTimeStr[12] = "--:--:--";

// Signal
int   lastRSSI = -120;
float lastSNR  = 0;
int   battPercent = 0;
int   lastMicRms = 0;

// LVGL display flush buffers (PSRAM-allocated)
static lv_color_t *lvBuf1 = nullptr;
static lv_color_t *lvBuf2 = nullptr;
#define LV_DISP_W 320
#define LV_DISP_H 240
#define LV_BUF_LINES 40

static lv_display_t *lvDisp = nullptr;
static lv_indev_t   *lvKeypad = nullptr;

// Keypad queue for LVGL
static volatile uint32_t pendingLvKey = 0;
static volatile bool     pendingLvKeyPressed = false;

// ════════════════════════════════════════════════════════════════════════════
//                            LVGL HOOKS
// ════════════════════════════════════════════════════════════════════════════

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushPixels((uint16_t*)px, w * h);
    tft.endWrite();
    lv_display_flush_ready(disp);
}

static void lvgl_keypad_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    if (pendingLvKey) {
        data->key = pendingLvKey;
        data->state = pendingLvKeyPressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        pendingLvKey = 0;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ════════════════════════════════════════════════════════════════════════════
//                            KEYBOARD + TRACKBALL
// ════════════════════════════════════════════════════════════════════════════
static bool altHeld = false;

static char readKeyboard() {
    Wire.requestFrom((uint8_t)KB_I2C_ADDR, (uint8_t)1);
    return Wire.available() ? (char)Wire.read() : 0;
}

// Map physical input to LVGL key code
static uint32_t mapToLvKey(char c) {
    switch (c) {
        case 0x05: return LV_KEY_UP;     // T-Deck arrows
        case 0x06: return LV_KEY_DOWN;
        case 0x07: return LV_KEY_LEFT;
        case 0x08: return LV_KEY_RIGHT;  // (also backspace — check firmware)
        case '\n':
        case '\r': return LV_KEY_ENTER;
        case 0x1B: return LV_KEY_ESC;
        default:   return 0;
    }
}

static void readTrackball() {
    // Trackball pins are active-low pulses
    // For LVGL nav: treat directional pulses as arrow key presses
    static bool lastU, lastD, lastL, lastR, lastC;
    bool u = !digitalRead(TRACKBALL_UP);
    bool d = !digitalRead(TRACKBALL_DN);
    bool l = !digitalRead(TRACKBALL_LT);
    bool r = !digitalRead(TRACKBALL_RT);
    // Click is shared with BOOT; sample but don't use during boot

    if (u && !lastU) { pendingLvKey = LV_KEY_UP;    pendingLvKeyPressed = true; }
    if (d && !lastD) { pendingLvKey = LV_KEY_DOWN;  pendingLvKeyPressed = true; }
    if (l && !lastL) { pendingLvKey = LV_KEY_LEFT;  pendingLvKeyPressed = true; }
    if (r && !lastR) { pendingLvKey = LV_KEY_RIGHT; pendingLvKeyPressed = true; }
    lastU = u; lastD = d; lastL = l; lastR = r;
}

// ════════════════════════════════════════════════════════════════════════════
//                            AES-256-GCM
// ════════════════════════════════════════════════════════════════════════════
static int aesEncrypt(const uint8_t *plain, size_t plainLen, uint8_t *out, size_t outBuf) {
    if (outBuf < plainLen + AES_IV_LEN + AES_TAG_LEN) return -1;
    esp_fill_random(out, AES_IV_LEN);
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, AES_KEY, 256);
    int ret = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, plainLen,
                                         out, AES_IV_LEN, nullptr, 0,
                                         plain, out + AES_IV_LEN,
                                         AES_TAG_LEN, out + AES_IV_LEN + plainLen);
    mbedtls_gcm_free(&ctx);
    return (ret == 0) ? (AES_IV_LEN + plainLen + AES_TAG_LEN) : -1;
}

static int aesDecrypt(const uint8_t *in, size_t inLen, uint8_t *plain, size_t plainBuf) {
    if (inLen < (size_t)(AES_IV_LEN + AES_TAG_LEN + 1)) return -1;
    size_t cLen = inLen - AES_IV_LEN - AES_TAG_LEN;
    if (plainBuf < cLen) return -1;
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, AES_KEY, 256);
    int ret = mbedtls_gcm_auth_decrypt(&ctx, cLen, in, AES_IV_LEN, nullptr, 0,
                                        in + AES_IV_LEN + cLen, AES_TAG_LEN,
                                        in + AES_IV_LEN, plain);
    mbedtls_gcm_free(&ctx);
    return (ret == 0) ? (int)cLen : -1;
}

// ════════════════════════════════════════════════════════════════════════════
//                            ES7210 + I2S
// ════════════════════════════════════════════════════════════════════════════
static void es7210_w(uint8_t r, uint8_t v) {
    Wire.beginTransmission(ES7210_I2C_ADDR);
    Wire.write(r); Wire.write(v);
    Wire.endTransmission();
}

static void initES7210() {
    es7210_w(0x00, 0xFF); delay(20);
    es7210_w(0x01, 0x20); es7210_w(0x02, 0x00);
    es7210_w(0x03, 0x01); es7210_w(0x04, 0x01);
    es7210_w(0x06, 0x00);
    es7210_w(0x14, 0x00); es7210_w(0x15, 0x00);
    es7210_w(0x11, 0xFF); es7210_w(0x12, 0xFF);
    delay(10);
}

static void initI2SMic() {
    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 8000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4, .dma_buf_len = 160,
        .use_apll = false, .tx_desc_auto_clear = false, .fixed_mclk = 0
    };
    i2s_pin_config_t p = {
        .mck_io_num = ES7210_MCLK, .bck_io_num = ES7210_SCK,
        .ws_io_num = ES7210_LRCK, .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = ES7210_DIN
    };
    i2s_driver_install(I2S_NUM_1, &cfg, 0, NULL);
    i2s_set_pin(I2S_NUM_1, &p);
}

static void initI2SSpeaker() {
    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 8000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4, .dma_buf_len = 160,
        .use_apll = false, .tx_desc_auto_clear = true, .fixed_mclk = 0
    };
    i2s_pin_config_t p = {
        .mck_io_num = I2S_PIN_NO_CHANGE, .bck_io_num = I2S_OUT_BCK,
        .ws_io_num = I2S_OUT_WS, .data_out_num = I2S_OUT_DATA,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &p);
}

// ════════════════════════════════════════════════════════════════════════════
//                            RADIO CONFIG SWITCHING
// ════════════════════════════════════════════════════════════════════════════
static void applyFrequency(int idx) {
    if (idx < 0 || idx >= FREQ_PRESET_COUNT) return;
    currentFreqIdx = idx;
    const FreqPreset &f = FREQ_PRESETS[idx];
    radio.setFrequency(f.freqMHz);
    radio.setOutputPower(f.maxPowerDbm);
    duty.setLimitSeconds(f.dutyCyclePercent == 1 ? 36 : 360);
    radio.startReceive();
}

static void applyCodec(int idx) {
    if (idx < 0 || idx >= CODEC_PRESET_COUNT) return;
    if (c2) codec2_destroy(c2);
    c2 = codec2_create(CODEC_PRESETS[idx].codec2Mode);
    currentCodecIdx = idx;
}

// ════════════════════════════════════════════════════════════════════════════
//                            VOICE TX / RX
// ════════════════════════════════════════════════════════════════════════════
static void transmitVoice() {
    if (!duty.canTransmit()) {
        ui.showToast("Duty cycle reached. Wait.", 1500);
        return;
    }

    const CodecPreset &cp = CODEC_PRESETS[currentCodecIdx];
    int framesPerPacket = 1;  // one Codec2 frame per LoRa packet for low latency

    size_t br = 0;
    unsigned long txStart = millis();

    // Capture one frame
    i2s_read(I2S_NUM_1, micBuf, cp.samplesPerFrame * sizeof(int16_t), &br, portMAX_DELAY);

    // Noise gate
    lastMicRms = gate.processFrame(micBuf, cp.samplesPerFrame);

    // Codec2 encode
    codec2_encode(c2, codec2Buf, micBuf);

    // Build packet [magic][seq][AES-GCM(IV+cipher+tag)]
    static uint8_t seq = 0;
    uint8_t pkt[2 + AES_IV_LEN + MAX_BYTES_PER_PACKET + AES_TAG_LEN];
    pkt[0] = PKT_MAGIC;
    pkt[1] = seq++;
    int encLen = aesEncrypt(codec2Buf, cp.bytesPerFrame, pkt + 2, sizeof(pkt) - 2);
    if (encLen < 0) return;

    radio.transmit(pkt, 2 + encLen);
    radio.startReceive();

    duty.addTxTime(millis() - txStart);
}

static uint8_t lastRxSeq = 0xFF;
static bool    seqInit   = false;

static void receiveVoice(uint8_t *pkt, int len) {
    if (len < 2 + AES_IV_LEN + AES_TAG_LEN + 1) return;
    if (pkt[0] != PKT_MAGIC) return;

    // Replay protection — seq must be strictly increasing (with wraparound)
    uint8_t rxSeq = pkt[1];
    if (seqInit) {
        uint8_t diff = rxSeq - lastRxSeq;
        if (diff == 0 || diff > 32) {
            Serial.printf("[WARN] Replay packet dropped seq=%d last=%d\n", rxSeq, lastRxSeq);
            return;
        }
    }
    lastRxSeq = rxSeq;
    seqInit   = true;

    const CodecPreset &cp = CODEC_PRESETS[currentCodecIdx];

    uint8_t plain[MAX_BYTES_PER_PACKET];
    int decLen = aesDecrypt(pkt + 2, len - 2, plain, sizeof(plain));
    if (decLen != cp.bytesPerFrame) return;   // wrong codec or auth fail

    codec2_decode(c2, spkBuf, plain);
    vol.apply(spkBuf, cp.samplesPerFrame);

    size_t wr = 0;
    i2s_write(I2S_NUM_0, spkBuf, cp.samplesPerFrame * sizeof(int16_t), &wr, portMAX_DELAY);
}

// ════════════════════════════════════════════════════════════════════════════
//                            UI CALLBACKS
// ════════════════════════════════════════════════════════════════════════════
static void cbFreqChange(int idx)   { applyFrequency(idx); }
static void cbCodecChange(int idx)  { applyCodec(idx); }
static void cbVolumeChange(int pct) { vol.setLevel(pct); }
static void cbGateChange(int rms)   { gate.setThreshold(rms); }
static void cbGateToggle(bool en)   { gate.setEnabled(en); }
static void cbBtToggle(bool en) {
    if (en && !btMgr.isRunning()) {
        btMgr.begin("TDeck-Walkie",
            [](const BluetoothMgr::ConfigCmd &c) {
                applyFrequency(c.freqIdx);
                applyCodec(c.codecIdx);
                vol.setLevel(c.volume);
                gate.setThreshold(c.noiseGate * 30);
            },
            [](const uint8_t *k) {
                memcpy(AES_KEY, k, 32);
                Preferences p;
                p.begin("walkie", false);
                p.putBytes("aes256key", AES_KEY, 32);
                p.end();
                ui.showToast("Key received via BLE!", 2000);
            });
        ui.showToast("BLE advertising", 1500);
    } else if (!en && btMgr.isRunning()) {
        btMgr.stop();
        ui.showToast("BLE off", 1500);
    }
}
static void cbPttRequest(bool pressed) { pttUI = pressed; }
static void cbKeyManagerOpen() {
    // Generate a fresh random key
    esp_fill_random(AES_KEY, 32);
    Preferences p;
    p.begin("walkie", false);
    p.putBytes("aes256key", AES_KEY, 32);
    p.end();
    char msg[64];
    snprintf(msg, sizeof(msg), "New key generated.\nFingerprint: %02X%02X..%02X%02X",
             AES_KEY[0], AES_KEY[1], AES_KEY[30], AES_KEY[31]);
    ui.showToast(msg, 3000);
}
static void cbMenuOpen() { /* handled inside UI */ }

// ════════════════════════════════════════════════════════════════════════════
//                            GPS / BATTERY
// ════════════════════════════════════════════════════════════════════════════
static void parseGpsByte(char c) {
    if (c == '\n') {
        gpsBuf[gpsBufIdx] = 0;
        if (strncmp(gpsBuf, "$GPRMC", 6) == 0 || strncmp(gpsBuf, "$GNRMC", 6) == 0) {
            char *tok = strtok(gpsBuf, ",");
            int field = 0;
            float rLat = 0, rLon = 0;
            char latD = 'N', lonD = 'E';
            char tf[12] = "";
            while (tok && field < 8) {
                switch (field) {
                    case 1: strncpy(tf, tok, 11); break;
                    case 2: gpsValid = (tok[0] == 'A'); break;
                    case 3: rLat = atof(tok); break;
                    case 4: latD = tok[0]; break;
                    case 5: rLon = atof(tok); break;
                    case 6: lonD = tok[0]; break;
                }
                tok = strtok(NULL, ","); field++;
            }
            if (gpsValid) {
                int la = (int)(rLat / 100); gpsLat = la + (rLat - la*100)/60.0f;
                if (latD == 'S') gpsLat = -gpsLat;
                int lo = (int)(rLon / 100); gpsLon = lo + (rLon - lo*100)/60.0f;
                if (lonD == 'W') gpsLon = -gpsLon;
                if (strlen(tf) >= 6)
                    snprintf(gpsTimeStr, sizeof(gpsTimeStr), "%c%c:%c%c:%c%c",
                             tf[0],tf[1],tf[2],tf[3],tf[4],tf[5]);
            }
        }
        gpsBufIdx = 0;
    } else if (gpsBufIdx < 127) {
        gpsBuf[gpsBufIdx++] = c;
    }
}

static int readBattery() {
    int raw = analogRead(BAT_ADC);
    float v = (raw / 4095.0f) * 3.3f * 2.0f;
    int pct = (int)((v - 3.3f) / (4.2f - 3.3f) * 100.0f);
    return constrain(pct, 0, 100);
}

// ════════════════════════════════════════════════════════════════════════════
//                            SETUP / LOOP
// ════════════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.println("[T-Deck+] Boot");

    pinMode(BOARD_POWERON, OUTPUT);     digitalWrite(BOARD_POWERON, HIGH);
    pinMode(BOARD_TFT_BACKLIGHT,OUTPUT);digitalWrite(BOARD_TFT_BACKLIGHT,HIGH);
    pinMode(TRACKBALL_UP, INPUT_PULLUP);
    pinMode(TRACKBALL_DN, INPUT_PULLUP);
    pinMode(TRACKBALL_LT, INPUT_PULLUP);
    pinMode(TRACKBALL_RT, INPUT_PULLUP);
    delay(100);

    Wire.begin(I2C_SDA, I2C_SCL);
    Serial1.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    analogReadResolution(12);

    SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

    // TFT
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    // LVGL
    lv_init();
    // Allocate double-buffer in PSRAM
    size_t bufSz = LV_DISP_W * LV_BUF_LINES * sizeof(lv_color_t);
    lvBuf1 = (lv_color_t*)heap_caps_malloc(bufSz, MALLOC_CAP_SPIRAM);
    lvBuf2 = (lv_color_t*)heap_caps_malloc(bufSz, MALLOC_CAP_SPIRAM);
    if (!lvBuf1 || !lvBuf2) {
        // Fallback to internal RAM if PSRAM unavailable
        lvBuf1 = (lv_color_t*)malloc(bufSz);
        lvBuf2 = (lv_color_t*)malloc(bufSz);
    }
    lvDisp = lv_display_create(LV_DISP_W, LV_DISP_H);
    lv_display_set_buffers(lvDisp, lvBuf1, lvBuf2, bufSz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(lvDisp, lvgl_flush_cb);

    lvKeypad = lv_indev_create();
    lv_indev_set_type(lvKeypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(lvKeypad, lvgl_keypad_read_cb);

    // Radio
    int rs = radio.beginFSK(FREQ_PRESETS[0].freqMHz, FSK_BITRATE, FSK_FREQ_DEV,
                             FSK_RX_BW, FREQ_PRESETS[0].maxPowerDbm, FSK_PREAMBLE);
    if (rs != RADIOLIB_ERR_NONE) {
        Serial.printf("[ERR] Radio init: %d\n", rs);
        while (1) delay(100);
    }
    uint8_t syncWord[] = {0xA5, 0x7E};
    radio.setSyncWord(syncWord, 2);
    radio.setCRC(2);
    radio.startReceive();

    // Audio
    initES7210();
    initI2SMic();
    initI2SSpeaker();

    // Codec2 initial
    applyCodec(0);

    // Seed AES_KEY from secrets.h as the factory default
    memcpy(AES_KEY, SECRETS_AES_KEY, 32);

    // Override with NVS key if one has been generated/saved
    Preferences p;
    p.begin("walkie", true);
    if (p.getBytesLength("aes256key") == 32) {
        p.getBytes("aes256key", AES_KEY, 32);
        Serial.println("[OK] AES key loaded from NVS.");
    } else {
        Serial.println("[INFO] Using secrets.h default. Press ALT+K to generate a unique key.");
    }
    p.end();

    // UI
    UICallbacks cb = {
        .onFreqChange = cbFreqChange,
        .onCodecChange = cbCodecChange,
        .onVolumeChange = cbVolumeChange,
        .onGateChange = cbGateChange,
        .onGateToggle = cbGateToggle,
        .onBtToggle = cbBtToggle,
        .onPttRequest = cbPttRequest,
        .onKeyManagerOpen = cbKeyManagerOpen,
        .onMenuOpen = cbMenuOpen,
    };
    ui.begin(cb);

    lv_indev_set_group(lvKeypad, lv_group_get_default());

    Serial.println("[OK] Ready. SPACE=PTT  Trackball=Nav  Click=Activate");
}

void loop() {
    static unsigned long lastUiPush = 0;
    static unsigned long lastBtPush = 0;

    // ── Keyboard ──
    char k = readKeyboard();
    if (k == 0x01) { altHeld = true; }
    else if (altHeld && k != 0) {
        char u = toupper(k);
        if      (u == 'M') ui.openSettings();
        else if (u == 'K') cbKeyManagerOpen();
        else if (u == 'F') applyFrequency((currentFreqIdx + 1) % FREQ_PRESET_COUNT);
        else if (u == 'C') applyCodec((currentCodecIdx + 1) % CODEC_PRESET_COUNT);
        altHeld = false;
    } else if (k != 0) {
        // Push to LVGL keypad
        uint32_t lvk = mapToLvKey(k);
        if (lvk) {
            pendingLvKey = lvk;
            pendingLvKeyPressed = true;
        }
        pttHardware = (k == ' ');
        if (k != ' ') altHeld = false;
    } else {
        pttHardware = false;
    }

    // ── Trackball nav ──
    readTrackball();

    // ── GPS ──
    while (Serial1.available()) parseGpsByte(Serial1.read());

    // ── Duty cycle tick ──
    duty.update();

    // ── TX / RX ──
    bool wantTx = (pttHardware || pttUI) && duty.canTransmit();
    if (wantTx) {
        txActive = true;
        rxActive = false;
        transmitVoice();
    } else {
        txActive = false;
        if (radio.available()) {
            int len = radio.getPacketLength();
            if (len > 0 && len < 128) {
                uint8_t buf[128];
                if (radio.readData(buf, len) == RADIOLIB_ERR_NONE) {
                    lastRSSI = (int)radio.getRSSI();
                    lastSNR  = radio.getSNR();
                    rxActive = true;
                    receiveVoice(buf, len);
                }
            }
            radio.startReceive();
        } else if (millis() - lastUiPush > 200) {
            rxActive = false;
        }
    }

    // ── Push UI state @ 5 Hz ──
    unsigned long now = millis();
    if (now - lastUiPush > 200) {
        battPercent = readBattery();
        UIState s = {};
        s.freqIdx = currentFreqIdx;
        s.codecIdx = currentCodecIdx;
        s.volume = vol.getLevel();
        s.noiseGateThreshold = gate.getThreshold();
        s.noiseGateEnabled = gate.isEnabled();
        s.bluetoothEnabled = btMgr.isRunning();
        s.btConnected = (btMgr.connectedClients() > 0);
        s.gpsValid = gpsValid;
        s.gpsLat = gpsLat; s.gpsLon = gpsLon;
        strncpy(s.gpsTime, gpsTimeStr, sizeof(s.gpsTime));
        s.battPercent = battPercent;
        s.rssi = lastRSSI;
        s.snr  = lastSNR;
        s.txActive = txActive;
        s.rxActive = rxActive;
        s.noiseGateOpen = gate.isOpen();
        s.micRms = lastMicRms;
        s.dutyUsedSec = duty.getUsedSeconds();
        s.dutyLimitSec = duty.getLimitSeconds();
        s.dutyPercent = duty.getUsedPercent();
        s.dutyBlocked = !duty.canTransmit();
        ui.setState(s);
        lastUiPush = now;

        // BLE status notify
        if (btMgr.isRunning() && now - lastBtPush > 1000) {
            BluetoothMgr::StatusReport sr = {};
            sr.freqHz = (uint32_t)(FREQ_PRESETS[currentFreqIdx].freqMHz * 1e6f);
            sr.rssi = lastRSSI; sr.snr = (int8_t)lastSNR;
            sr.dutyPercent = s.dutyPercent; sr.battPercent = battPercent;
            sr.codecIdx = currentCodecIdx; sr.volume = vol.getLevel();
            sr.txActive = txActive ? 1 : 0;
            btMgr.publishStatus(sr);
            lastBtPush = now;
        }
    }

    // ── LVGL pump ──
    ui.tick();
}
