# 📡 UDM Packet Radio Internet Gateway (PRIG)

<div align="center">

![Version](https://img.shields.io/badge/version-2.1.0-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
![Platform](https://img.shields.io/badge/platform-ESP32-red.svg)
![Protocol](https://img.shields.io/badge/protocol-AX.25%20%7C%20KISS-orange.svg)

*Moderne Packet Radio Gateway Lösung mit Smart-Polling und GitHub-basiertem OTA-Update System*

[🚀 Features](#-features) • [📋 Installation](#-installation) • [🔧 Konfiguration](#-konfiguration) • [📖 Dokumentation](#-dokumentation)

</div>

---

## 🎯 Überblick

Das **UDM Packet Radio Internet Gateway (PRIG)** ist eine moderne, ESP32-basierte Lösung für Packet Radio Kommunikation. Es verbindet traditionelle AX.25/KISS Packet Radio Terminals mit modernen Internet-Technologien und bietet dabei ultra-schnelle Nachrichtenübertragung durch ein innovatives Smart-Polling System.

### 🌟 Highlights

- **⚡ Smart-Polling**: Nachrichten kommen in 0.5-2 Sekunden an (vs. 10+ Sekunden traditionell)
- **🔄 GitHub OTA-Updates**: Vollautomatische Firmware-Updates über GitHub
- **📱 Responsive Web-Interface**: Optimiert für Desktop und Mobile
- **🖥️ TSTHost Kompatibilität**: 100% kompatibel mit gängigen Terminal-Programmen
- **🔍 Erweiterte Debug-Features**: Umfassendes Logging mit HEX-Dumps und KISS-Dekodierung
- **⚙️ EEPROM-Konfiguration**: Persistente Einstellungen und Versionsverwaltung

---

## 🚀 Features

### 📡 Packet Radio
- **AX.25/KISS Protokoll**: Vollständige Implementierung für RS232 @ 2400 Baud
- **Binary Frame Processing**: Exakte KISS-Frame-Verarbeitung mit TSTHost-Kompatibilität
- **Smart Frame Generation**: Automatische AX.25-Header-Generierung
- **Multi-Client Support**: Verwaltung mehrerer Packet Radio Clients

### 🌐 Internet Gateway
- **HTTP/WiFi Bridge**: Nahtlose Verbindung zwischen Packet Radio und Internet
- **Base64 Encoding**: Sichere Übertragung binärer KISS-Frames
- **Smart-Polling System**: Adaptive Polling-Intervalle (500ms-2s)
- **Database Notifications**: Effiziente Nachrichten-Queue-Verwaltung

### 🔧 Administration
- **Web-Interface**: Moderne, responsive Benutzeroberfläche
- **Client Management**: Einfache Verwaltung von Packet Radio Clients
- **Server Configuration**: Zentrale Konfiguration von Callsign und Log-Level
- **Real-time Monitoring**: Live-Monitor mit erweiterten Debug-Informationen

### 🔄 OTA-System
- **GitHub Integration**: Automatische Updates direkt von GitHub-Repository
- **Version Management**: EEPROM-basierte Versionsverwaltung
- **Configurable URLs**: Flexible Repository-Konfiguration
- **Web-Interface Control**: Update-Steuerung über Web-Interface

---

## 📋 Installation

### 🔌 Hardware-Anforderungen

- **ESP32 Development Board** (z.B. ESP32-WROOM-32)
- **OLED Display** (128x64, I2C)
- **RS232 Interface** (MAX3232 oder ähnlich)
- **Packet Radio Modem** (TNC, etc.)

### 📚 Software-Anforderungen

#### ESP32 Client
- Arduino IDE 1.8.19+
- ESP32 Board Package
- Bibliotheken:
  - `WiFi` (ESP32 Core)
  - `HTTPClient` (ESP32 Core)
  - `ArduinoJson` (6.x)
  - `Adafruit_SSD1306`
  - `EEPROM` (ESP32 Core)

#### Server
- **Web Server**: Apache/Nginx mit PHP 7.4+
- **Database**: MySQL 5.7+ / MariaDB 10.3+
- **PHP Extensions**: `mysqli`, `json`, `curl`

### 🛠️ Setup-Schritte

#### 1. ESP32 Client Flash
```bash
# 1. Arduino IDE öffnen
# 2. ESP32 Board auswählen
# 3. ino/udm-prig-client.ino öffnen
# 4. WiFi-Credentials konfigurieren
# 5. Upload
```

#### 2. Server Deployment
```bash
# Files auf Web Server hochladen
scp -r * user@server:/var/www/html/udm-prig/

# Datenbank erstellen
mysql -u root -p
CREATE DATABASE if0_37988924_udmdb;
```

#### 3. Database Schema
```sql
-- Server Konfiguration
CREATE TABLE server_config (
    id INT PRIMARY KEY,
    callsign VARCHAR(32),
    loglevel INT DEFAULT 1
);
INSERT INTO server_config VALUES (1, 'PRGSRV', 1);

-- Client Liste
CREATE TABLE clients (
    id INT AUTO_INCREMENT PRIMARY KEY,
    callsign VARCHAR(32) UNIQUE,
    status ENUM('active','inactive') DEFAULT 'active'
);

-- Monitor Logs
CREATE TABLE monitor (
    id INT AUTO_INCREMENT PRIMARY KEY,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
    message TEXT,
    loglevel INT DEFAULT 1
);

-- Smart-Polling Notifications
CREATE TABLE notifications (
    id INT AUTO_INCREMENT PRIMARY KEY,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    data TEXT,
    processed BOOLEAN DEFAULT FALSE
);

-- Admin Benutzer
CREATE TABLE admin_users (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(50) UNIQUE,
    password_hash VARCHAR(255)
);
```

---

## 🔧 Konfiguration

### 📡 ESP32 Client

#### WiFi & Server
```cpp
// WiFi Konfiguration
const char* ssid = "IhrWiFiName";
const char* password = "IhrWiFiPasswort";

// Server URLs
String serverURL = "https://ihr-server.com/udm-prig";
String otaURL = "https://github.com/ihr-user/repository";
```

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

### 🌐 Server

#### Datenbankverbindung (`db.php`)
```php
$server = "sql309.infinityfree.com";
$username = "if0_37988924";
$password = "ihr_db_passwort";
$database = "if0_37988924_udmdb";
```

#### Admin-Account erstellen
```bash
# Über Web-Interface: /login.php
# Standard: admin / admin (nach Login ändern!)
```

---

## 📖 Dokumentation

### 🔄 Smart-Polling System

Das revolutionäre Smart-Polling System reduziert Latenz dramatisch:

- **Adaptive Intervalle**: 500ms (aktiv) bis 2s (idle)
- **Database Notifications**: Effiziente Queue-Verwaltung
- **Ultra-fast Delivery**: Nachrichten in Sekunden statt Minuten

```mermaid
graph LR
A[Terminal] --> B[ESP32]
B --> C[Smart Polling]
C --> D[PHP Backend]
D --> E[MySQL Queue]
E --> F[KISS Frame]
F --> B
```

### 📡 AX.25/KISS Protokoll

Vollständige KISS-Implementation mit TSTHost-Kompatibilität:

```
KISS Frame Format:
[C0] [00] [AX.25 Header] [Control] [PID] [Data] [C0]
```

### 🔄 OTA Update System

GitHub-basiertes Update-System mit EEPROM-Versionsverwaltung:

1. **Version Check**: Vergleich GitHub vs. EEPROM
2. **Download**: Direkt von GitHub Repository
3. **Validation**: Firmware-Integrität prüfen
4. **Flash**: Sichere Installation
5. **Verification**: EEPROM-Update

---

## 🎛️ Nutzung

### 📱 Web-Interface

1. **Login**: `https://ihr-server.com/udm-prig/login.php`
2. **Dashboard**: Client-Status, Server-Monitor, Konfiguration
3. **Client Management**: Hinzufügen/Entfernen von Packet Radio Clients
4. **OTA Updates**: Firmware-Updates mit einem Klick

### 🖥️ Terminal Software

Kompatibel mit allen gängigen Packet Radio Terminals:
- **TSTHost** ✅
- **UISS** ✅  
- **AGW Packet Engine** ✅
- **Winlink Express** ✅

### 📊 Monitoring

Real-time Monitoring mit erweiterten Debug-Features:
- **Live Log**: Alle Systemereignisse in Echtzeit
- **HEX Dumps**: Binäre Frame-Analyse
- **KISS Decoding**: Detaillierte Protokoll-Analyse
- **Performance Metrics**: Smart-Polling Statistiken

---

## 🔬 Erweiterte Features

### 🐛 Debug System
```cpp
// Log Levels
0: ERROR   - Nur kritische Fehler
1: INFO    - Allgemeine Informationen
2: WARNING - Warnungen und wichtige Events  
3: DEBUG   - Detaillierte Debug-Informationen mit HEX-Dumps
```

### ⚡ Performance Optimierung
- **Smart Intervals**: Adaptive Polling basierend auf Aktivität
- **Database Indexing**: Optimierte Queries für schnelle Abfragen
- **Connection Pooling**: Effiziente Datenbankverbindungen
- **Binary Processing**: Direkte KISS-Frame-Verarbeitung

### 🔐 Sicherheit
- **Session Management**: Sichere Admin-Authentifizierung
- **SQL Injection Protection**: Prepared Statements
- **Input Validation**: Umfassende Eingabevalidierung
- **HTTPS Ready**: SSL/TLS Unterstützung

---

## 🤝 Beiträge

Wir freuen uns über Beiträge! Bitte beachten Sie:

1. **Fork** das Repository
2. **Branch** für Ihr Feature erstellen (`git checkout -b feature/AmazingFeature`)
3. **Commit** Ihre Änderungen (`git commit -m 'Add AmazingFeature'`)
4. **Push** zum Branch (`git push origin feature/AmazingFeature`)
5. **Pull Request** öffnen

### 🐛 Bug Reports
Nutzen Sie GitHub Issues für Bug Reports mit:
- Detaillierte Beschreibung
- Schritte zur Reproduktion
- Erwartetes vs. tatsächliches Verhalten
- System-Informationen

---

## 📞 Support

- **GitHub Issues**: Für Bugs und Feature Requests
- **Documentation**: Siehe `docs/` Verzeichnis
- **Community**: Packet Radio Forums und Discord

---

## 📜 Lizenz

Dieses Projekt steht unter der MIT Lizenz - siehe [LICENSE](LICENSE) für Details.

---

## 🙏 Danksagungen

- **Packet Radio Community** für kontinuierliches Feedback
- **ESP32 Community** für Hardware-Unterstützung  
- **Open Source Maintainers** für fantastische Bibliotheken

---

<div align="center">

**⭐ Wenn Ihnen dieses Projekt gefällt, geben Sie ihm einen Stern auf GitHub! ⭐**

*Entwickelt mit ❤️ für die Packet Radio Community*

</div>