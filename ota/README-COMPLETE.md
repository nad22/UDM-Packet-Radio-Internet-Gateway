# OTA Firmware Update System für ESP32 UDM-PRIG Client

Komplettes OTA-System mit GitHub-basierter Firmware-Verteilung.

## Schnell-Setup

### 1. GitHub Repository erstellen
1. Gehen Sie zu github.com und erstellen Sie ein neues Repository
2. Repository Name: z.B. "udm-prig-firmware" 
3. Wählen Sie "Public" (oder "Private" mit Token-Setup)

### 2. ESP32 Code konfigurieren
Öffnen Sie `ino/udm-prig-client.ino` und ändern Sie:
```cpp
const char* otaRepoUrl = "https://raw.githubusercontent.com/IHR-USERNAME/IHR-REPO/main/ota";
```

### 3. Erste Firmware kompilieren
1. Arduino IDE → Öffne `udm-prig-client.ino`
2. Board: **ESP32 Dev Module**
3. **Sketch** → **Export compiled Binary**
4. Kopiere die .bin Datei nach `ota/udm-prig-client.ino.esp32.bin`

### 4. Zu GitHub hochladen
```bash
git add .
git commit -m "Initial firmware v1.0.1"
git push origin main
```

## Update-Workflow

### Neue Version erstellen
1. **Windows:** `update_version.bat 1.0.2`
2. **Linux/Mac:** `./update_version.sh 1.0.2`
3. Arduino IDE → Kompiliere → Export Binary
4. Kopiere .bin zu `ota/udm-prig-client.ino.esp32.bin`
5. Git commit & push

### ESP32 Update installieren
1. ESP32 Web-Interface öffnen
2. **"Nach Updates suchen"** klicken
3. Bei verfügbarem Update: **"Update installieren"**
4. Automatischer Neustart nach erfolgreichem Update

## Verzeichnisstruktur
```
ota/
├── version.txt                    # Aktuelle Version
├── udm-prig-client.ino.esp32.bin # Kompilierte Firmware
├── update_version.bat             # Windows Version-Tool
├── update_version.sh              # Linux/Mac Version-Tool
└── README.md                      # Diese Anleitung
```

## Web-Interface Features

ESP32 zeigt im Web-Interface:
- **Aktuelle Version:** Lokale Firmware-Version
- **OTA Repository:** GitHub Raw URL
- **"Nach Updates suchen":** Prüft GitHub auf neue Versionen
- **"Update installieren":** Lädt und installiert neue Firmware

## Debugging

ESP32 Monitor zeigt OTA-Status:
```
[INFO] OTA: Checking for updates...
[INFO] OTA: Remote version: 1.0.2, Local version: 1.0.1
[INFO] OTA: Starting firmware update...
[INFO] OTA: Update successful! Restarting...
```

## Fehlerbehandlung

**404 Error:** Repository/Datei nicht gefunden
- Prüfe GitHub URL im ESP32 Code
- Stelle sicher dass version.txt und .bin existieren

**Update schlägt fehl:** Ungültige Firmware
- Kompiliere Firmware neu in Arduino IDE
- Verwende "ESP32 Dev Module" Board-Einstellung

**Private Repository:** Erfordert Token-Authentication
- Verwende Public Repository oder
- Implementiere GitHub Personal Access Token

## Automatische Features

- **Startup Check:** ESP32 prüft automatisch beim Booten auf Updates
- **Web Interface:** Manuelle Update-Prüfung über Browser
- **Progress Display:** Real-time Status im Monitor
- **Automatic Restart:** Nach erfolgreichem Update