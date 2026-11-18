# UDM Packet Radio Internet Gateway (PRIG) - PlatformIO Project

This project was migrated from the Arduino IDE to **PlatformIO** for the ESP32 Dev Module.

## 📁 Project Structure

```
├── platformio.ini          # PlatformIO configuration
├── src/
│   └── main.cpp            # Main source code (formerly .ino)
├── include/                # Header files
├── lib/                    # Local libraries
├── data/                   # SPIFFS/LittleFS files
├── scripts/                # Build scripts
│   └── version_increment.py
├── ota/                    # OTA update files
│   ├── README.md
│   ├── udm-prig-client.ino.bin
│   └── version.txt
└── README_PLATFORMIO.md    # This file
```

## 🚀 Getting Started

### 1. PlatformIO Installation

**VS Code Extension:**
- Open VS Code
- Install the "PlatformIO IDE" extension
- Open the project directory

**CLI Installation:**
```bash
pip install platformio
```

### 2. Build the Project

**VS Code:**
- Press `Ctrl+Shift+P` → "PlatformIO: Build"
- Or click the build icon in the status bar

**CLI:**
```bash
pio run
```

### 3. Upload to ESP32

**VS Code:**
- `Ctrl+Shift+P` → "PlatformIO: Upload"

**CLI:**
```bash
pio run --target upload
```

### 4. Serial Monitor

**VS Code:**
- `Ctrl+Shift+P` → "PlatformIO: Serial Monitor"

**CLI:**
```bash
pio device monitor
```

## ⚙️ Configuration

### Board Settings

The project is configured for **ESP32 Dev Module**:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
```

### COM Port Configuration

Windows users may need to adjust the COM port:

```ini
monitor_port = COM3    # Set your port
upload_port = COM3     # Set your port
```

### Build Options

Important build flags:

```ini
build_flags = 
    -DCORE_DEBUG_LEVEL=3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DMQTT_MAX_PACKET_SIZE=1024
```

## 📚 Libraries

Required libraries are installed automatically:

- **Adafruit GFX Library** - Display graphics
- **Adafruit SH110X** - OLED Display SH1106G
- **Adafruit SSD1306** - OLED Display SSD1306
- **ArduinoMqttClient** - MQTT communication
- **ArduinoJson** - JSON Parsing

## 🔧 Hardware Configuration

### Pin Assignments

```cpp
// I2S Audio (MAX98357A)
#define I2S_DOUT_PIN 25    // DIN
#define I2S_BCLK_PIN 26    // BCLK  
#define I2S_LRC_PIN  27    // LRC
#define I2S_GAIN_PIN 33    // GAIN

// RS232 (HardwareSerial)
// GPIO16 = RX
// GPIO17 = TX

// I2C Display
// GPIO21 = SDA (default)
// GPIO22 = SCL (default)

// Status LED
#define LED_PIN 2
```

### Hardware Requirements

- **ESP32 Development Board** (WROOM-32 or similar)
- **OLED Display 128x64** (SH1106G or SSD1306, I2C)
- **MAX98357A I2S Audio Amplifier** (optional, for AFSK)
- **RS232 Interface** (MAX3232 or similar)
- **Level Shifter** 3.3V ↔ 5V (for higher signal levels)

## 🌐 MQTT Configuration

The system simulates **CB radio** using MQTT:

- **Channels:** 1-40 (udmprig/rf/1 to udmprig/rf/40)
- **Encryption:** XOR with shared secret
- **Audio:** Bell 202 AFSK (1200/2200 Hz)
- **Protocol:** KISS over RS232

## 🔄 OTA Updates

**Automatic at boot:**
- Updates are checked at startup and installed only when the release version is
  numerically newer than the installed version. Older versions require manual flashing.
- Repository: GitHub (`ota/` directory)

For local test builds, add `-DUDMPRIG_DISABLE_AUTO_OTA` to the PlatformIO
`build_flags` to prevent automatic replacement by a release. Regular builds
retain automatic update checks.

**Web interface:**
- http://udmprig-client.local/
- http://[ESP32-IP]/

## 📊 Monitoring

### Web-Interface

The system provides a web configuration interface:

- **Hardware-Info:** CPU, Memory, WiFi Status
- **MQTT Status:** Connection, CB channel, encryption
- **Audio Settings:** Volume, AFSK frequencies
- **Display Control:** Brightness, type
- **Serial Monitor:** Live debug output

At startup the device attempts to connect to WiFi up to three times, each for
five seconds. Attempts and results appear on the OLED and serial monitor. After
three failures, the configuration portal starts. WiFi power saving remains
disabled after a WiFi reset.

Under **Config → Error sounds**, choose 1 = Off (default) or 2–4 for three
different ringtones. The setting persists and is also available in the
configuration portal. Selecting a ringtone plays a preview without saving it.
Errors logged at `ERROR` level play through the MAX98357A; simultaneous pending
errors are combined into one sound. Error sounds are independent of packet audio
simulation but share its volume and hardware gain. Sound is unavailable until
I2S has initialized successfully or if I2S fails.

### Debug Output

```ini
monitor_speed = 115200    # ESP32 debug serial port
```

Log level can be set in the web interface:
- **0:** Error only
- **1:** Info (default)
- **2:** Warning
- **3:** Debug (verbose)

## 🛠️ Development

### Increment Version

The legacy version increment script can be invoked with:

```bash
python scripts/version_increment.py
```

### Custom Libraries

Local libraries in the `lib/` directory:

```
lib/
├── MyLibrary/
│   ├── MyLibrary.h
│   └── MyLibrary.cpp
└── README
```

### SPIFFS/LittleFS Files

Web assets in the `data/` directory:

```bash
pio run --target uploadfs
```

## 🐛 Troubleshooting

### Common Problems

**1. COM port not found:**
```bash
pio device list
```

**2. Library not found:**
```bash
pio lib install
```

**3. Board not detected:**
```ini
# In platformio.ini
upload_port = COM3
upload_speed = 115200
```

**4. Memory problems:**
```ini
# Adjust partitions
board_build.partitions = default.csv
```

### Enable Debug Output

```ini
build_flags = 
    -DCORE_DEBUG_LEVEL=5
    -DARDUINO_USB_CDC_ON_BOOT=1
```

## 📋 Command Reference

```bash
# Project information
pio project config

# Clean build
pio run --target clean

# Upload with monitor
pio run --target upload --target monitor

# Update dependencies
pio lib update

# Board information
pio boards esp32dev

# Environment-specific builds
pio run -e esp32dev
```

## 🔗 Useful Links

- **PlatformIO Docs:** https://docs.platformio.org/
- **ESP32 Arduino:** https://docs.espressif.com/projects/arduino-esp32/
- **Original Repository:** https://github.com/nad22/UDM-Packet-Radio-Internet-Gateway

## 📄 License

MIT License - See the original README.md

---

**73 de AT1NAD**  
*www.pukepals.com*