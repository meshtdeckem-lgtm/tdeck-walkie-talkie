# T-Deck Plus LoRa Walkie-Talkie — v2 (LVGL)

Encrypted voice walkie-talkie firmware for **LILYGO T-Deck Plus** (868 MHz EU variant).

## Features

- **LVGL 9.x UI** — modern dark theme, focus-based navigation
- **On-screen PTT button** — visually indicates key-out state (red when transmitting)
- **EU duty cycle bar** — tracks rolling 1-hour TX time, hard-blocks at limit (36 s on 1% sub-bands, 360 s on 869.525 MHz g3 high-power band)
- **5 selectable Codec2 quality presets** — 700 / 1200 / 1600 / 2400 / 3200 bps
- **5 selectable EU 868 MHz channels** — including high-duty-cycle g3 sub-band
- **Noise gate** — adjustable RMS threshold, hysteresis, smooth gain ramp
- **On-screen volume slider** — logarithmic curve, applied to RX audio
- **AES-256-GCM encryption** — per-packet IV, authenticated, hardware-accelerated
- **BLE GATT service** — remote config + secure key delivery from phone
- **GPS display** — lat / lon / UTC time
- **Battery / RSSI / SNR / signal bars**
- **Frequency hotkey** — ALT+F cycles channels
- **Codec hotkey** — ALT+C cycles bitrates
- **Key manager** — generate via hardware RNG, save to NVS, share via BLE

## Controls

| Input | Action |
|-------|--------|
| `SPACE` | PTT (hold to transmit) |
| Trackball UP/DN/LT/RT | Navigate focus between widgets |
| Trackball click | Activate focused widget |
| `ALT + M` | Open Settings dialog |
| `ALT + K` | Generate new AES key |
| `ALT + F` | Cycle frequency |
| `ALT + C` | Cycle codec quality |

> The T-Deck Plus screen is **not** touch-capable. The on-screen PTT button is a state indicator — activate it via trackball click or just use SPACE.

## Hardware Notes

- ESP32-S3 has BLE only (no Classic / A2DP), so wireless headset audio is not supported. BLE is used here for config + key exchange.
- 868.0–868.6 MHz: 1% duty cycle, 14 dBm max — used for default channels.
- 869.525 MHz (g3): 10% duty cycle, 27 dBm max — high-power sub-band.

## Build

### Local (PlatformIO)

```bash
# Clone Codec2 (not on registry)
mkdir -p lib
git clone https://github.com/sh123/esp32_codec2 lib/esp32_codec2

# Build + flash
pio run -e tdeck_plus --target upload
```

### CI (automatic on every push)

Push to GitHub → Actions tab → download artifacts:

| Artifact | Use |
|----------|-----|
| `tdeck_walkie_M5Launcher.bin` | Drop on SD card, install via M5 Launcher |
| `tdeck_walkie_merged.bin` | Flash standalone via `web.esphome.io` or esptool |

## Flashing via M5 Launcher

1. Install M5 Launcher firmware on the T-Deck Plus once (from `bmorcelli.github.io/Launcher/`)
2. Copy `tdeck_walkie_M5Launcher.bin` to the root of your microSD card
3. Insert SD, boot into Launcher, navigate the SD file list, select the .bin
4. Launcher installs it to the OTA slot and reboots into it

## Packet Format

```
[1 B magic=0xA5] [1 B seq] [12 B AES-GCM IV] [N B ciphertext] [16 B GCM tag]
```

Where N = Codec2 frame size (4–8 bytes depending on bitrate).
Total: 34–38 bytes per voice frame — well within FSK MTU.

## Files

```
tdeck_walkie_talkie.ino   main entry + setup/loop
lv_conf.h                 LVGL config (keypad input, no touch)
platformio.ini            build config
src/
  radio_config.h          frequency + codec presets
  duty_cycle.h            rolling-window TX tracker
  audio_dsp.h             noise gate + volume control
  bluetooth_mgr.h         BLE GATT service
  ui_lvgl.h               UI declarations
  ui_lvgl.cpp             UI implementation
.github/workflows/build.yml  CI for both .bin artifacts
```

## Security Notes

- Default compiled-in AES key is a placeholder — generate your own via ALT+K on first boot.
- BLE key transfer uses LE Secure Connections (ECDH + MITM protection).
- Bootstrap key in `key_manager.h` (used for LoRa key broadcast pairing) should be changed before deployment.
- This is hobby-grade encryption. Don't rely on it for safety-critical comms.
