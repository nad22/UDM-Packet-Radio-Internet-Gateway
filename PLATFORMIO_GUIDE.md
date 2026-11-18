# PlatformIO Quick Start Guide for UDM-PRIG

## 🚀 Quick Start

### 1. Install PlatformIO

**In VS Code:**
- Install the "PlatformIO IDE" extension
- Restart VS Code
- Open the project directory

**Command line:**
```bash
pip install platformio
```

### 2. Open the Project

**VS Code:**
- File → Open Folder → select the project directory
- PlatformIO automatically detects `platformio.ini`

**CLI:**
```bash
cd /path/to/project
pio project init
```

## ⚙️ Important Commands

### Build
```bash
# Standard Build
pio run

# Debug Build
pio run -e esp32dev_debug

# Release Build  
pio run -e esp32dev_release

# Clean Build
pio run --target clean
```

### Upload to ESP32
```bash
# Standard Upload
pio run --target upload

# With serial monitor
pio run --target upload --target monitor

# OTA upload (if configured)
pio run -e esp32dev_ota --target upload
```

### Serial Monitor
```bash
# Start monitor
pio device monitor

# With a specific baud rate
pio device monitor --baud 115200

# List available ports
pio device list
```

### Manage Libraries
```bash
# Install all
pio lib install

# Specific library
pio lib install "Adafruit GFX Library"

# Check for updates
pio lib update

# List libraries
pio lib list
```

## 🛠️ Development

### Switch Environment
```bash
# Debug-Modus
pio run -e esp32dev_debug

# Release-Modus
pio run -e esp32dev_release

# Default (as defined in default_envs)
pio run
```

### Update Dependencies
```bash
# Update platform
pio platform update espressif32

# Update all packages
pio pkg update
```

### Code Analysis
```bash
# Static analysis (if configured)
pio check

# Run tests
pio test
```

## 📋 Useful Information

### Board Information
```bash
# List ESP32 boards
pio boards esp32

# Specific board information
pio boards esp32dev
```

### Show Project Configuration
```bash
# Complete configuration
pio project config

# Specific environment only
pio project config --environment esp32dev
```

### Firmware-Info
```bash
# Build information
pio run --verbose

# Size analysis
pio run --target size
```

## 🔧 COM Port Problems (Windows)

### Find the Port
```bash
# List all serial ports
pio device list

# Device Manager (Windows)
devmgmt.msc
```

### Set the Port in platformio.ini
```ini
[env:esp32dev]
monitor_port = COM3
upload_port = COM3
```

### Driver Problems
- CP2102/CP2104: [Silicon Labs Drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
- CH340/CH341: [WCH Drivers](http://www.wch.cn/downloads/CH341SER_EXE.html)
- FTDI: [FTDI Drivers](https://ftdichip.com/drivers/)

## 📊 Monitor Output

### Set Debug Level
```ini
# In platformio.ini
build_flags = 
    -DCORE_DEBUG_LEVEL=3  ; 0=None, 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Verbose
```

### Use Log Filters
```ini
monitor_filters = 
    esp32_exception_decoder  ; Decode stack traces
    time                     ; Add timestamps
    log2file                 ; Save to file
```

## 🚨 Common Problems

### "Permission denied" (Linux/Mac)
```bash
# Add user to the dialout group
sudo usermod -a -G dialout $USER
# Log out and back in
```

### "Board not found"
```bash
# Check that the USB cable supports data
# Boot mode: hold the BOOT button while powering on
# Press and release the reset button
```

### "Library not found"
```bash
# Reinstall libraries
pio lib install --force

# Clear cache
pio system prune --cache
```

### Memory Problems
```ini
# Use a larger partition in platformio.ini
board_build.partitions = huge_app.csv
```

## 🔗 Documentation

- **PlatformIO:** https://docs.platformio.org/
- **ESP32 Arduino:** https://docs.espressif.com/projects/arduino-esp32/
- **Library Registry:** https://registry.platformio.org/
- **Board Explorer:** https://docs.platformio.org/en/latest/boards/espressif32/esp32dev.html

---

**Tip:** The VS Code extension provides a graphical interface and makes most commands available as buttons.