# PlatformIO Quick Start Guide für UDM-PRIG

## 🚀 Schnellstart

### 1. PlatformIO installieren

**In VS Code:**
- Extension "PlatformIO IDE" installieren
- VS Code neustarten
- Das Projekt-Verzeichnis öffnen

**Kommandozeile:**
```bash
pip install platformio
```

### 2. Projekt öffnen

**VS Code:**
- File → Open Folder → Projekt-Verzeichnis auswählen
- PlatformIO erkennt automatisch die `platformio.ini`

**CLI:**
```bash
cd /pfad/zum/projekt
pio project init
```

## ⚙️ Wichtige Befehle

### Kompilieren
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

### Upload zum ESP32
```bash
# Standard Upload
pio run --target upload

# Mit Serial Monitor
pio run --target upload --target monitor

# OTA Upload (wenn konfiguriert)
pio run -e esp32dev_ota --target upload
```

### Serial Monitor
```bash
# Monitor starten
pio device monitor

# Mit bestimmter Baudrate
pio device monitor --baud 115200

# Verfügbare Ports anzeigen
pio device list
```

### Bibliotheken verwalten
```bash
# Alle installieren
pio lib install

# Bestimmte Bibliothek
pio lib install "Adafruit GFX Library"

# Updates prüfen
pio lib update

# Bibliotheken anzeigen
pio lib list
```

## 🛠️ Entwicklung

### Environment wechseln
```bash
# Debug-Modus
pio run -e esp32dev_debug

# Release-Modus
pio run -e esp32dev_release

# Standard (wie in default_envs definiert)
pio run
```

### Abhängigkeiten aktualisieren
```bash
# Platform aktualisieren
pio platform update espressif32

# Alle Pakete aktualisieren
pio pkg update
```

### Code-Analyse
```bash
# Static Analysis (wenn konfiguriert)
pio check

# Tests ausführen
pio test
```

## 📋 Nützliche Infos

### Board-Informationen
```bash
# ESP32 Boards anzeigen
pio boards esp32

# Spezifische Board-Info
pio boards esp32dev
```

### Projekt-Konfiguration anzeigen
```bash
# Komplette Konfiguration
pio project config

# Nur bestimmtes Environment
pio project config --environment esp32dev
```

### Firmware-Info
```bash
# Build-Informationen
pio run --verbose

# Größen-Analyse
pio run --target size
```

## 🔧 COM-Port Probleme (Windows)

### Port herausfinden
```bash
# Alle seriellen Ports anzeigen
pio device list

# Geräte-Manager (Windows)
devmgmt.msc
```

### Port in platformio.ini setzen
```ini
[env:esp32dev]
monitor_port = COM3
upload_port = COM3
```

### Treiber-Probleme
- CP2102/CP2104: [Silicon Labs Drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
- CH340/CH341: [WCH Drivers](http://www.wch.cn/downloads/CH341SER_EXE.html)
- FTDI: [FTDI Drivers](https://ftdichip.com/drivers/)

## 📊 Monitor-Ausgaben

### Debug-Level einstellen
```ini
# In platformio.ini
build_flags = 
    -DCORE_DEBUG_LEVEL=3  ; 0=None, 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Verbose
```

### Log-Filter verwenden
```ini
monitor_filters = 
    esp32_exception_decoder  ; Stack-Traces entschlüsseln
    time                     ; Zeitstempel hinzufügen
    log2file                 ; In Datei speichern
```

## 🚨 Häufige Probleme

### "Permission denied" (Linux/Mac)
```bash
# Benutzer zur dialout Gruppe hinzufügen
sudo usermod -a -G dialout $USER
# Abmelden/Anmelden erforderlich
```

### "Board not found"
```bash
# USB-Kabel prüfen (muss Daten unterstützen)
# Boot-Modus: BOOT-Taste gedrückt halten beim Einschalten
# Reset-Taste drücken und loslassen
```

### "Library not found"
```bash
# Bibliotheken neu installieren
pio lib install --force

# Cache leeren
pio system prune --cache
```

### Memory-Probleme
```ini
# In platformio.ini größere Partition
board_build.partitions = huge_app.csv
```

## 🔗 Dokumentation

- **PlatformIO:** https://docs.platformio.org/
- **ESP32 Arduino:** https://docs.espressif.com/projects/arduino-esp32/
- **Library Registry:** https://registry.platformio.org/
- **Board Explorer:** https://docs.platformio.org/en/latest/boards/espressif32/esp32dev.html

---

**Tipp:** Für die erste Verwendung empfiehlt sich die VS Code Extension, da sie eine grafische Oberfläche bietet und die meisten Befehle über Buttons verfügbar macht.