# UDM Packet Radio Internet Gateway (PRIG) - PlatformIO Project

Dieses Projekt wurde von der Arduino IDE auf **PlatformIO** für das ESP32 Dev Module umstrukturiert.

## 📁 Projektstruktur

```
├── platformio.ini          # PlatformIO Konfiguration
├── src/
│   └── main.cpp            # Haupt-Quellcode (ehemals .ino)
├── include/                # Header-Dateien (falls benötigt)
├── lib/                    # Lokale Bibliotheken
├── data/                   # SPIFFS/LittleFS Dateien
├── scripts/                # Build-Skripte
│   └── version_increment.py
├── ota/                    # OTA Update-Dateien
│   ├── README.md
│   ├── udm-prig-client.ino.bin
│   └── version.txt
└── README_PLATFORMIO.md    # Diese Datei
```

## 🚀 Erste Schritte

### 1. PlatformIO Installation

**VS Code Extension:**
- Öffne VS Code
- Installiere die "PlatformIO IDE" Extension
- Lade das Projekt-Verzeichnis

**CLI Installation:**
```bash
pip install platformio
```

### 2. Projekt kompilieren

**VS Code:**
- Drücke `Ctrl+Shift+P` → "PlatformIO: Build"
- Oder klicke auf das Build-Symbol in der Statusleiste

**CLI:**
```bash
pio run
```

### 3. Auf ESP32 hochladen

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

## ⚙️ Konfiguration

### Board-Einstellungen

Das Projekt ist für **ESP32 Dev Module** konfiguriert:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
```

### COM-Port Konfiguration

Windows-Benutzer müssen möglicherweise den COM-Port anpassen:

```ini
monitor_port = COM3    # Passe deinen Port an
upload_port = COM3     # Passe deinen Port an
```

### Build-Optionen

Die wichtigsten Build-Flags:

```ini
build_flags = 
    -DCORE_DEBUG_LEVEL=3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DMQTT_MAX_PACKET_SIZE=1024
```

## 📚 Bibliotheken

Alle benötigten Bibliotheken werden automatisch installiert:

- **Adafruit GFX Library** - Display-Grafiken
- **Adafruit SH110X** - OLED Display SH1106G
- **Adafruit SSD1306** - OLED Display SSD1306
- **ArduinoMqttClient** - MQTT Kommunikation
- **ArduinoJson** - JSON Parsing

## 🔧 Hardware-Konfiguration

### Pin-Belegung

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
// GPIO21 = SDA (Standard)
// GPIO22 = SCL (Standard)

// Status LED
#define LED_PIN 2
```

### Hardware-Anforderungen

- **ESP32 Development Board** (WROOM-32 oder ähnlich)
- **OLED Display 128x64** (SH1106G oder SSD1306, I2C)
- **MAX98357A I2S Audio Amplifier** (optional, für AFSK)
- **RS232 Interface** (MAX3232 oder ähnlich)
- **Level Shifter** 3,3V ↔ 5V (für höhere Signalpegel)

## 🌐 MQTT Konfiguration

Das System arbeitet als **CB-Funk Simulation** mit MQTT:

- **Kanäle:** 1-40 (udmprig/rf/1 bis udmprig/rf/40)
- **Verschlüsselung:** XOR mit shared secret
- **Audio:** Bell 202 AFSK (1200/2200 Hz)
- **Protokoll:** KISS über RS232

## 🔄 OTA Updates

**Automatisch beim Boot:**
- Updates werden beim Start automatisch geprüft und nur installiert, wenn die
  Release-Version numerisch neuer ist als die aktuell installierte Version.
  Ältere Versionen lassen sich nur manuell flashen.
- Repository: GitHub (`ota/` Verzeichnis)

Für lokale Test-Builds kann `-DUDMPRIG_DISABLE_AUTO_OTA` zu den PlatformIO
`build_flags` hinzugefügt werden. Dadurch wird der Teststand nicht automatisch
durch ein Release ersetzt; reguläre Builds behalten die automatische Prüfung.

**Manuell über Web-Interface:**
- http://udmprig-client.local/
- http://[ESP32-IP]/

## 📊 Monitoring

### Web-Interface

Das System bietet eine vollständige Web-Konfiguration:

- **Hardware-Info:** CPU, Memory, WiFi Status
- **MQTT Status:** Verbindung, CB-Kanal, Verschlüsselung  
- **Audio-Einstellungen:** Lautstärke, AFSK-Frequenzen
- **Display-Kontrolle:** Helligkeit, Typ
- **Serial Monitor:** Live-Debug-Ausgaben

Beim Start versucht das Gerät die WLAN-Verbindung bis zu dreimal jeweils
fünf Sekunden lang. Versuch und Ergebnis erscheinen auf dem OLED und im
seriellen Monitor; nach drei Fehlschlägen startet das Konfigurationsportal.
Der WLAN-Energiesparmodus bleibt auch nach einem WLAN-Reset deaktiviert.

Unter **Config → Fehlertöne** stehen vier Einstellungen zur Wahl:
1 = Aus (Standard), 2–4 = drei unterschiedliche Klingeltöne. Die Einstellung
wird dauerhaft gespeichert und ist auch im Konfigurationsportal verfügbar.
Bei der Auswahl eines Klingeltons wird eine Hörprobe abgespielt, ohne die
Einstellung bereits zu speichern.
Fehler mit Log-Level `ERROR` werden über den MAX98357A ausgegeben; mehrere
gleichzeitig anstehende Fehler werden zu einem Ton zusammengefasst. Der Ton
ist unabhängig von der Paket-Audio-Simulation, verwendet aber deren Lautstärke
und Hardware-Verstärkung. Vor erfolgreicher I2S-Initialisierung oder bei einem
I2S-Fehler ist keine Tonausgabe möglich.

### Debug-Ausgaben

```ini
monitor_speed = 2400    # Entspricht der RS232-Baudrate
```

Log-Level über Web-Interface einstellbar:
- **0:** Error only
- **1:** Info (Standard)
- **2:** Warning
- **3:** Debug (verbose)

## 🛠️ Entwicklung

### Version automatisch erhöhen

Das Build-Script erhöht automatisch die Patch-Version:

```bash
python scripts/version_increment.py
```

### Eigene Bibliotheken

Lokale Libraries in `lib/` Verzeichnis:

```
lib/
├── MyLibrary/
│   ├── MyLibrary.h
│   └── MyLibrary.cpp
└── README
```

### SPIFFS/LittleFS Dateien

Web-Assets in `data/` Verzeichnis:

```bash
pio run --target uploadfs
```

## 🐛 Fehlerbehebung

### Häufige Probleme

**1. COM-Port nicht gefunden:**
```bash
pio device list
```

**2. Bibliothek nicht gefunden:**
```bash
pio lib install
```

**3. Board nicht erkannt:**
```ini
# In platformio.ini
upload_port = COM3
upload_speed = 115200
```

**4. Memory-Probleme:**
```ini
# Partitions anpassen
board_build.partitions = default.csv
```

### Debug-Ausgaben aktivieren

```ini
build_flags = 
    -DCORE_DEBUG_LEVEL=5
    -DARDUINO_USB_CDC_ON_BOOT=1
```

## 📋 Befehle-Übersicht

```bash
# Projekt-Informationen
pio project config

# Clean build
pio run --target clean

# Upload mit Monitor
pio run --target upload --target monitor

# Dependencies aktualisieren
pio lib update

# Board-Informationen
pio boards esp32dev

# Environment-spezifische Builds
pio run -e esp32dev
```

## 🔗 Nützliche Links

- **PlatformIO Docs:** https://docs.platformio.org/
- **ESP32 Arduino:** https://docs.espressif.com/projects/arduino-esp32/
- **Original Repository:** https://github.com/nad22/UDM-Packet-Radio-Internet-Gateway

## 📄 Lizenz

MIT License - Siehe original README.md

---

**73 de AT1NAD**  
*www.pukepals.com*