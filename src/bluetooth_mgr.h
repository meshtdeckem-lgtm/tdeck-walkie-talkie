/**
 * bluetooth_mgr.h — BLE GATT service for config + secure key exchange
 *
 * Why BLE not Classic BT:
 *   ESP32-S3 supports BLE 5 only (no A2DP / Classic profiles).
 *   So wireless audio headset isn't possible without external hardware.
 *
 * What this provides:
 *   - A custom GATT service exposing live status + config write
 *   - Phone app or BLE explorer (e.g. nRF Connect) can:
 *       • Read current freq / codec / RSSI / duty %
 *       • Write new AES key (32 bytes, securely paired)
 *       • Set frequency, codec, volume remotely
 *   - Pairing uses BLE bonding (LE Secure Connections) — keys protected
 *     by ECDH on the BLE link.
 *
 * Service UUID:        e5f00001-9b1d-4f3a-a5d2-1a4e8b7c9d0e
 * Status char (R/N):   e5f00002-…  → packed struct (16 bytes)
 * Config char (R/W):   e5f00003-…  → packed struct (8 bytes)
 * AES key char  (W):   e5f00004-…  → 32 bytes (encrypted-link only)
 */
#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

// External handles from main
extern uint8_t AES_KEY[32];

class BluetoothMgr {
public:
    // Status update struct (packed for BLE transport)
    struct __attribute__((packed)) StatusReport {
        uint32_t freqHz;         // current frequency in Hz
        int16_t  rssi;
        int8_t   snr;
        uint8_t  dutyPercent;
        uint8_t  battPercent;
        uint8_t  codecIdx;
        uint8_t  volume;
        uint8_t  txActive;       // 1 if currently transmitting
        uint8_t  _pad[3];
    };

    struct __attribute__((packed)) ConfigCmd {
        uint8_t freqIdx;     // index into FREQ_PRESETS
        uint8_t codecIdx;    // index into CODEC_PRESETS
        uint8_t volume;      // 0-100
        uint8_t noiseGate;   // 0-100 (mapped to RMS threshold)
        uint8_t btMute;      // reserved
        uint8_t _pad[3];
    };

    // Callbacks owner sets to receive commands from BLE
    using ConfigCb = std::function<void(const ConfigCmd&)>;
    using KeyCb    = std::function<void(const uint8_t* key32)>;

    void begin(const char *deviceName, ConfigCb onConfig, KeyCb onKey) {
        onConfigCb_ = onConfig;
        onKeyCb_    = onKey;

        NimBLEDevice::init(deviceName);
        NimBLEDevice::setPower(ESP_PWR_LVL_P9);    // +9 dBm BLE TX
        NimBLEDevice::setSecurityAuth(true, true, true);  // bond + MITM + SC

        server_ = NimBLEDevice::createServer();
        service_ = server_->createService("e5f00001-9b1d-4f3a-a5d2-1a4e8b7c9d0e");

        // Status — read + notify
        statusChar_ = service_->createCharacteristic(
            "e5f00002-9b1d-4f3a-a5d2-1a4e8b7c9d0e",
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

        // Config — read + write (encrypted)
        configChar_ = service_->createCharacteristic(
            "e5f00003-9b1d-4f3a-a5d2-1a4e8b7c9d0e",
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE |
            NIMBLE_PROPERTY::WRITE_ENC);
        configChar_->setCallbacks(new ConfigCharCB(this));

        // Key — write only, encrypted + authenticated
        keyChar_ = service_->createCharacteristic(
            "e5f00004-9b1d-4f3a-a5d2-1a4e8b7c9d0e",
            NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC |
            NIMBLE_PROPERTY::WRITE_AUTHEN);
        keyChar_->setCallbacks(new KeyCharCB(this));

        service_->start();

        NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
        adv->addServiceUUID(service_->getUUID());
        adv->setName(deviceName);
        adv->enableScanResponse(true);
        adv->start();

        running_ = true;
    }

    void stop() {
        if (running_) {
            NimBLEDevice::getAdvertising()->stop();
            NimBLEDevice::deinit(true);
            running_ = false;
        }
    }

    bool isRunning() const { return running_; }
    int  connectedClients() const {
        return server_ ? server_->getConnectedCount() : 0;
    }

    void publishStatus(const StatusReport &s) {
        if (!running_ || !statusChar_) return;
        statusChar_->setValue((const uint8_t*)&s, sizeof(s));
        if (connectedClients() > 0) statusChar_->notify();
    }

private:
    class ConfigCharCB : public NimBLECharacteristicCallbacks {
    public:
        ConfigCharCB(BluetoothMgr *m) : mgr_(m) {}
        void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &info) override {
            auto val = c->getValue();
            if (val.length() == sizeof(ConfigCmd) && mgr_->onConfigCb_) {
                ConfigCmd cmd;
                memcpy(&cmd, val.data(), sizeof(cmd));
                mgr_->onConfigCb_(cmd);
            }
        }
        BluetoothMgr *mgr_;
    };

    class KeyCharCB : public NimBLECharacteristicCallbacks {
    public:
        KeyCharCB(BluetoothMgr *m) : mgr_(m) {}
        void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &info) override {
            auto val = c->getValue();
            if (val.length() == 32 && mgr_->onKeyCb_) {
                mgr_->onKeyCb_(val.data());
            }
        }
        BluetoothMgr *mgr_;
    };

    NimBLEServer         *server_     = nullptr;
    NimBLEService        *service_    = nullptr;
    NimBLECharacteristic *statusChar_ = nullptr;
    NimBLECharacteristic *configChar_ = nullptr;
    NimBLECharacteristic *keyChar_    = nullptr;
    ConfigCb              onConfigCb_;
    KeyCb                 onKeyCb_;
    bool                  running_ = false;
};
