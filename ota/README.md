# OTA Update System

## Dateien für Over-The-Air Updates

### version.txt
Enthält die aktuelle Firmware-Version (z.B. "1.0.1")

### UDMPRG-Client.ino.bin
Die kompilierte Firmware-Binärdatei für ESP32

## GitHub Raw URLs

Die OTA-URLs zeigen auf GitHub Raw Files:
- Version: `https://raw.githubusercontent.com/DEIN-USERNAME/DEIN-REPO/main/ota/version.txt`
- Firmware: `https://raw.githubusercontent.com/DEIN-USERNAME/DEIN-REPO/main/ota/UDMPRG-Client.ino.bin`

## Firmware-Kompilierung

Um eine neue .bin-Datei zu erstellen:
1. Arduino IDE öffnen
2. Sketch -> Export compiled Binary
3. .bin-Datei ins ota/ Verzeichnis kopieren
4. version.txt aktualisieren
5. Alles committen und pushen

## Automatisches Update

Der ESP32 prüft beim Start automatisch auf Updates.