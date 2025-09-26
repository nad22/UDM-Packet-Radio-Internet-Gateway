# 📡 UDM Packet Radio Internet Gateway (PRIG)

<div align="center">

![Version](https://img.shields.io/badge/version-2.1.3-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
![Platform](https://img.shields.io/badge/platform-ESP32-red.svg)
![Protocol](https://img.shields.io/badge/protocol-MQTT%20%7C%20AFSK%20%7C%20KISS-orange.svg)

*Modern Serverless Packet Radio Gateway with CB-Packet Radio Simulation and Studio-Quality AFSK Audio*

[🚀 Features](#-features) • [📋 Installation](#-installation) • [🔧 Configuration](#-configuration) • [📖 Documentation](#-documentation)

</div>

---

## 🎯 Overview

The **UDM Packet Radio Internet Gateway (PRIG)** is a ESP32-based solution that transforms traditional packet radio communication into a modern, serverless MQTT broadcast system. It simulates CB-Packet Radio behavior with channel selection, encrypted payload transmission, and authentic Bell 202 AFSK audio generation. It is used in combination with a TNC Software and a KISS compatible driver, e.g. TSTSHOST & tfpcx or flexnet

### 🌟 Key Features

- **📻 CB Packet Radio Simulation**: 40-channel broadcast system mimicking classic CB radio
- **� Encrypted Payloads**: XOR encryption with shared secret for secure communication
- **🎵 Studio-Quality AFSK**: I2S audio with MAX98357A for authentic Bell 202 sound
- **🌐 Serverless Architecture**: Pure MQTT broadcast, no server dependencies
- **� Professional UI**: 7-segment CB channel display with brightness control
- **⚙️ Advanced Configuration**: Thematic web interface with live controls

---

## 🚀 Features

### � CB Packet Radio MQTT System
- **40 Channel Support**: CB channels 1-40 with professional 7-segment display
- **Broadcast Architecture**: Serverless MQTT topics `udmprig/rf/1` to `udmprig/rf/40`
- **HiveMQ Cloud**: SSL/TLS encrypted MQTT broker connectivity
- **Channel Switching**: Live channel selection with instant topic switching

### 🔒 Security & Encryption
- **Payload Encryption**: XOR encryption with configurable shared secret
- **EEPROM Storage**: Persistent encryption keys and configuration
- **JSON Structure**: Clean message format with encrypted payload field
- **Selective Encryption**: Only payload data encrypted, metadata readable

### 🎵 Professional AFSK Audio
- **I2S Audio Output**: MAX98357A amplifier for studio-quality sound
- **Bell 202 Standard**: Authentic 1200/2200 Hz Mark/Space frequencies
- **Phase-Continuous**: Smooth frequency transitions for clean AFSK
- **Volume Control**: 0-100% adjustable audio level
- **TX/RX Differentiation**: Frequency offset for authentic radio feel

### 🌐 Network & Connectivity
- **WiFi Management**: WPA2 with connection monitoring and auto-reconnect
- **MQTT SSL/TLS**: Secure encrypted communication to cloud broker
- **mDNS Discovery**: Easy network discovery as `udm-prig.local`
- **Web Configuration**: Responsive HTML5 interface with real-time updates

### 🖥️ Display & Interface
- **OLED Support**: SH1106G and SSD1306 displays with auto-detection
- **7-Segment CB Display**: Professional channel indication (01-40)
- **Brightness Control**: 0-100% adjustable display brightness
- **Live Status**: Real-time TX/RX indicators and connection status

### 🔧 Advanced Configuration
- **Thematic Web UI**: Grouped configuration cards for better organization
- **EEPROM Persistence**: All settings stored in non-volatile memory
- **CPU Frequency**: Performance optimization (240/160/80 MHz)
- **Debug Levels**: Comprehensive logging with HEX dumps and protocol analysis
- **Real-time Monitoring**: Live-Monitor mit erweiterten Debug-Informationen

### 🔄 OTA-System
- **GitHub Integration**: Automatische Updates direkt von GitHub-Repository
- **Version Management**: EEPROM-basierte Versionsverwaltung
- **Configurable URLs**: Flexible Repository-Konfiguration
- **Web-Interface Control**: Update-Steuerung über Web-Interface

---

## 📋 Installation

### 🔌 Hardware-Reqirements

- **ESP32 Development Board** (z.B. ESP32-WROOM-32)
- **OLED Display** (128x64, I2C)
- **RS232 Interface** (MAX3232 oder ähnlich)
- **Packet Radio Modem** (TNC, etc.)


#### Hardware Pins
```cpp
// OLED Display (I2C)
#define SCREEN_ADDRESS 0x3C
#define OLED_RESET -1

// RS232 Serial
#define RS232_RX 16
#define RS232_TX 17
#define RS232_BAUD 2400
```


<div align="center">

**⭐ Wenn Ihnen dieses Projekt gefällt, geben Sie ihm einen Stern auf GitHub! ⭐**

*Entwickelt mit ❤️ für die Packet Radio Community*

</div>
