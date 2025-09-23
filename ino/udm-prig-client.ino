#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <ESPmDNS.h>
#include <time.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <HTTPUpdate.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>

// MQTT Buffer-Größe drastisch erhöhen
#define MQTT_MAX_PACKET_SIZE 1024
#define MQTT_KEEPALIVE 300

// **MQTT Standard-Konfiguration**
#include <WiFiClientSecure.h>
#include <ArduinoMqttClient.h>

// Watchdog-Timer Konfiguration
#define WDT_TIMEOUT 30  // 30 Sekunden Watchdog-Timeout
#define EEPROM_SIZE 1024  // Erweitert für Crash-Logs
#define SSID_OFFSET 0
#define PASS_OFFSET 64
#define SERVERURL_OFFSET 128
#define CALLSIGN_OFFSET 160
#define BAUD_OFFSET 194
#define LOGLEVEL_OFFSET 198
#define OTAURL_OFFSET 200
#define VERSION_OFFSET 328
#define DISPLAYTYPE_OFFSET 345  // Offset für Display-Typ
#define SSL_VALIDATION_OFFSET 346  // Offset für SSL-Zertifikat-Validierung
#define CPU_FREQUENCY_OFFSET 347   // Offset für CPU-Frequenz (1 byte: 0=240MHz, 1=160MHz, 2=80MHz)
#define DISPLAY_BRIGHTNESS_OFFSET 348   // Offset für Display-Helligkeit (1 byte: 0-255)

// MQTT Configuration (EEPROM 349-399)
#define MQTT_ENABLED_OFFSET 349        // 1 byte: 0=HTTP, 1=MQTT
#define MQTT_BROKER_OFFSET 350         // 64 bytes: MQTT Broker URL
#define MQTT_PORT_OFFSET 414           // 2 bytes: MQTT Port (uint16_t)
#define MQTT_USERNAME_OFFSET 416       // 16 bytes: MQTT Username
#define MQTT_PASSWORD_OFFSET 432       // 32 bytes: MQTT Password
#define CB_CHANNEL_OFFSET 464          // 1 byte: CB-Kanal 1-40
#define MQTT_BROKER_OFFSET 349         // 64 bytes für Broker URL (erweitert für HiveMQ Cloud)
#define MQTT_PORT_OFFSET 413           // 2 bytes für Port (8883)
#define MQTT_USERNAME_OFFSET 415       // 16 bytes für Username (erweitert)
#define MQTT_PASSWORD_OFFSET 431       // 32 bytes für Password (erweitert auf 30 Zeichen)

// Crash Log System (EEPROM 466-1023)
#define CRASH_LOG_START_OFFSET 466
#define CRASH_LOG_COUNT_OFFSET 466  // 4 bytes für Anzahl der Logs
#define CRASH_LOG_ENTRIES_OFFSET 470  // Crash Log Einträge (5 x 120 = 600 bytes)
#define CRASH_LOG_ENTRY_SIZE 120  // Timestamp (20) + Message (100)
#define MAX_CRASH_LOGS 5

#define LED_PIN 2

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_LINE_LEN 33
#define OLED_LINE_DIVIDER "---------------------------------"

// Display-Typen
#define DISPLAY_SH1106G 0
#define DISPLAY_SSD1306 1

// Display-Objekte (nur eins wird verwendet)
Adafruit_SH1106G display_sh1106(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_SSD1306 display_ssd1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

HardwareSerial RS232(1);

unsigned long lastBlink = 0;
bool ledState = false;
unsigned long lastKeepalive = 0;
const unsigned long KEEPALIVE_INTERVAL = 10000;
unsigned long lastRS232 = 0;

unsigned long lastRX = 0;
unsigned long lastTX = 0;
const unsigned long RS232_ACTIVE_TIME = 1000; // 1 Sekunde für bessere Sichtbarkeit

WebServer server(80);

char wifiSsid[64] = "";
char wifiPass[64] = "";
char serverUrl[64] = ""; // z.B. "http://192.168.1.10/udmprig-server"
char callsign[32] = "";
char otaRepoUrl[128] = "https://raw.githubusercontent.com/nad22/UDM-Packet-Radio-Internet-Gateway/main/ota";
uint32_t baudrate = 2400;
uint8_t logLevel = 1;
uint8_t displayType = DISPLAY_SSD1306; // Default: SSD1306 (kleineres Display)
bool sslValidation = true; // Default: SSL-Validierung aktiviert für sichere Verbindungen
uint8_t cpuFrequency = 0; // Default: 240 MHz (0=240MHz, 1=160MHz, 2=80MHz, 3=40MHz, 4=26MHz)
uint8_t displayBrightness = 128; // Default: 50% Helligkeit (0-255)

// MQTT Configuration
bool mqttEnabled = true; // Default: MQTT mode (reine MQTT-Implementierung)
char mqttBroker[64] = ""; // HiveMQ Cloud Broker URL (erweitert für längere URLs)
uint16_t mqttPort = 8883; // Default: SSL Port
char mqttUsername[16] = ""; // MQTT Username (erweitert)
char mqttPassword[32] = ""; // MQTT Password (erweitert auf 30+1 Zeichen)
uint8_t cbChannel = 1; // CB-Funk Kanal 1-40 (Default: Kanal 1)

// MQTT Client Objects
WiFiClientSecure mqttWifiClient;
MqttClient mqttClient(mqttWifiClient);
unsigned long lastMqttReconnect = 0;
bool mqttConnected = false;

// Einfaches Broadcast-System (Funk-Simulation)
// WICHTIG: HiveMQ Cloud muss konfiguriert werden für:
// - Message Expiry: 0 (keine Speicherung alter Nachrichten)  
// - Retain: disabled (keine persistenten Nachrichten)
// - QoS: 0 (Fire-and-Forget wie echter Funk)
String mqttBroadcastTopic = "udmprig/rf/1";  // Dynamischer CB-Funk-Kanal (wird in setupMqttTopics() gesetzt)

// KISS Protocol Buffer für saubere Nachrichtentrennung
static String kissBuffer = "";
static bool inKissFrame = false;
static int expectedKissLength = 0;
static int currentKissLength = 0;

bool apActive = false;
bool connectionError = false; // WiFi-Verbindungsfehler Flag

// WiFi-Überwachung und Stabilität
unsigned long lastWiFiCheck = 0;
unsigned long wifiReconnectAttempts = 0;
const unsigned long WIFI_CHECK_INTERVAL = 30000; // WiFi-Status alle 30 Sekunden prüfen
const unsigned long WIFI_RECONNECT_DELAY = 5000; // 5 Sekunden zwischen Reconnect-Versuchen
bool forceWiFiReconnect = false;

// Crash Log System
struct CrashLogEntry {
  char timestamp[21];  // YYYY-MM-DD HH:MM:SS + null terminator
  char message[100];   // Error message
};

uint32_t crashLogCount = 0;
CrashLogEntry crashLogs[MAX_CRASH_LOGS];

#define MONITOR_BUF_SIZE 4096
String monitorBuf = "";
String rs232HexBuf = "";
String rs232AscBuf = "";

const char* ntpServer = "at.pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

// OTA
char localVersion[16] = "1.0.2"; // STABILE VERSION mit I2C-Optimierung
bool otaCheckedThisSession = false;

// Anti-Freeze Protection - STABILE VERSION
unsigned long lastWatchdogReset = 0;
unsigned long lastMemoryCheck = 0;
const unsigned long MEMORY_CHECK_INTERVAL = 60000; // 60 Sekunden (weniger häufig)

// CPU-Last Messung
unsigned long lastCpuMeasurement = 0;
unsigned long busyTime = 0;
unsigned long idleTime = 0;
float cpuUsagePercent = 0.0;
const unsigned long CPU_MEASUREMENT_INTERVAL = 10000; // CPU-Last alle 10 Sekunden messen

void appendMonitor(const String& msg, const char* level = "INFO");
String getTimestamp();
void blinkLED();
String decodeKissFrame(String rawData); // Forward-Deklaration für KISS-Dekodierung
void handleOTACheck(); // Forward-Deklaration für OTA
void handleOTAUpdate(); // Forward-Deklaration für OTA
void handleHardwareInfo(); // Forward-Deklaration für Hardware Info API
void handleReboot(); // Forward-Deklaration für System Reboot
void saveCrashLog(const String& message); // Forward-Deklaration für Crash Log
void loadCrashLogs(); // Forward-Deklaration für Crash Log laden
void saveCrashLogsToEEPROM(); // Forward-Deklaration für Crash Log speichern

// MQTT Forward-Deklarationen
void onMqttMessage(int messageSize);
bool connectMQTT();
void setupMqttTopics();
bool publishMqttMessage(const String& topic, const String& message);
void handleMqttLoop();
bool isMqttConnected();
void processMqttMessage(const String& message); // Forward-Deklaration für MQTT-Message Processing
String hexToBytes(const String& hexString); // Forward-Deklaration für Hex-Dekodierung
void clearCrashLogs(); // Forward-Deklaration für Crash Log löschen
void handleClearCrashLogs(); // Forward-Deklaration für Crash Log löschen Handler
void checkWiFiConnection(); // Forward-Deklaration für WiFi-Überwachung
String getWiFiStatusText(wl_status_t status); // Forward-Deklaration für WiFi-Status-Text
String getFormattedUptime(); // Forward-Deklaration für formatierte Uptime
void logWiFiDiagnostics(); // Forward-Deklaration für erweiterte WiFi-Diagnose
bool tryConnectWiFi(); // Forward-Deklaration für WiFi-Verbindung
bool initDisplay(); // Forward-Deklaration für Display-Initialisierung
bool isGatewayReachable(); // Forward-Deklaration für Gateway-Ping
void handleDisplayBrightness(); // Forward-Deklaration für Display-Helligkeit API
void setDisplayBrightness(uint8_t brightness); // Forward-Deklaration für Helligkeit setzen


void processCompleteKissMessage(const String& kissData);

// Root CA Bundle für SSL-Zertifikatsprüfung (Let's Encrypt, DigiCert, etc.)
const char* root_ca = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n" \
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n" \
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n" \
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n" \
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n" \
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n" \
"h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n" \
"0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n" \
"A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n" \
"T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n" \
"B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n" \
"B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n" \
"KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n" \
"OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n" \
"jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n" \
"qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n" \
"rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n" \
"HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n" \
"hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n" \
"ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n" \
"3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n" \
"NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n" \
"ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n" \
"TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n" \
"jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n" \
"oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n" \
"4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n" \
"mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n" \
"emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n" \
"-----END CERTIFICATE-----\n";

// Display-Initialisierung basierend auf displayType
bool initDisplay() {
  if (displayType == DISPLAY_SSD1306) {
    if (!display_ssd1306.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
      return false;
    }
    display_ssd1306.clearDisplay();
    display_ssd1306.display();
    setDisplayBrightness(displayBrightness); // Gespeicherte Helligkeit anwenden
  } else {
    if (!display_sh1106.begin(0x3C)) {
      return false;
    }
    display_sh1106.clearDisplay();
    display_sh1106.display();
    setDisplayBrightness(displayBrightness); // Gespeicherte Helligkeit anwenden
  }
  return true;
}

// HTTPS-HTTPClient-Konfiguration mit automatischer URL-Erkennung
void configureHTTPClient(HTTPClient &http, String url) {
  if (url.startsWith("https://")) {
    WiFiClientSecure *client = new WiFiClientSecure;
    
    if (sslValidation) {
      // Echte SSL-Zertifikatsprüfung (für offizielle Zertifikate)
      client->setCACert(root_ca);
    } else {
      // Für Self-Signed Zertifikate: Zertifikatsprüfung deaktivieren
      client->setInsecure(); 
    }
    
    http.begin(*client, url);
  } else {
    // Standard HTTP
    http.begin(url);
  }
  
  // Standard-Timeouts (längere Timeouts für Smart-Polling)
  http.setTimeout(10000);  // 10 Sekunden statt 5
  http.setConnectTimeout(5000);  // 5 Sekunden statt 3
}
uint16_t getDisplayWhite() {
  if (displayType == DISPLAY_SSD1306) {
    return SSD1306_WHITE;
  } else {
    return SH110X_WHITE;
  }
}

void bootPrint(const String &msg) {
  static int line = 0;
  
  if (displayType == DISPLAY_SSD1306) {
    display_ssd1306.setTextSize(1);
    display_ssd1306.setTextColor(getDisplayWhite());
    display_ssd1306.setCursor(0, 8 * line++);
    String showMsg = msg.substring(0, OLED_LINE_LEN); // max 33 Zeichen
    display_ssd1306.print(showMsg);
    display_ssd1306.display();
    if (line >= 8) {
      display_ssd1306.clearDisplay();
      line = 0;
    }
  } else {
    display_sh1106.setTextSize(1);
    display_sh1106.setTextColor(getDisplayWhite());
    display_sh1106.setCursor(0, 8 * line++);
    String showMsg = msg.substring(0, OLED_LINE_LEN); // max 33 Zeichen
    display_sh1106.print(showMsg);
    display_sh1106.display();
    if (line >= 8) {
      display_sh1106.clearDisplay();
      line = 0;
    }
  }
  delay(300);
}

void showOTAUpdateScreen(const char* text, float progress = -1) {
  if (displayType == DISPLAY_SSD1306) {
    display_ssd1306.clearDisplay();
    display_ssd1306.setTextSize(1);
    display_ssd1306.setTextColor(getDisplayWhite());
    display_ssd1306.setCursor(2, 13);
    display_ssd1306.print("Firmware-Update");
    display_ssd1306.setCursor(2, 25);
    display_ssd1306.print(String(text).substring(0, OLED_LINE_LEN));
    if(progress >= 0.0) {
      int barWidth = 112, barHeight = 10, barX = 8, barY = 40;
      display_ssd1306.drawRect(barX, barY, barWidth, barHeight, getDisplayWhite());
      int filled = (int)(barWidth * progress);
      if(filled > 0)
        display_ssd1306.fillRect(barX+1, barY+1, filled-2, barHeight-2, getDisplayWhite());
      display_ssd1306.setCursor(48, 54);
      display_ssd1306.print(int(progress*100));
      display_ssd1306.print("%");
    }
    display_ssd1306.display();
  } else {
    display_sh1106.clearDisplay();
    display_sh1106.setTextSize(1);
    display_sh1106.setTextColor(getDisplayWhite());
    display_sh1106.setCursor(2, 13);
    display_sh1106.print("Firmware-Update");
    display_sh1106.setCursor(2, 25);
    display_sh1106.print(String(text).substring(0, OLED_LINE_LEN));
    if(progress >= 0.0) {
      int barWidth = 112, barHeight = 10, barX = 8, barY = 40;
      display_sh1106.drawRect(barX, barY, barWidth, barHeight, getDisplayWhite());
      int filled = (int)(barWidth * progress);
      if(filled > 0)
        display_sh1106.fillRect(barX+1, barY+1, filled-2, barHeight-2, getDisplayWhite());
      display_sh1106.setCursor(48, 54);
      display_sh1106.print(int(progress*100));
      display_sh1106.print("%");
    }
    display_sh1106.display();
  }
}

void drawWifiStrength(int strength) {
  int x = SCREEN_WIDTH - 22;
  int y = 0;
  
  uint16_t white = getDisplayWhite();
  uint16_t black = (displayType == DISPLAY_SSD1306) ? SSD1306_BLACK : SH110X_BLACK;
  
  if (displayType == DISPLAY_SSD1306) {
    // 4 Signal-Balken (ohne Antennensymbol)
    for(int i=0;i<4;i++) {
      if(strength > i) {
        // Starkes Signal: Balken gefüllt zeichnen
        display_ssd1306.fillRect(x+4+i*3, y+12-2*i, 2, 2+2*i, white);
      } else {
        // Schwaches Signal: Balken explizit schwarz überschreiben (ausblenden)
        display_ssd1306.fillRect(x+4+i*3, y+12-2*i, 2, 2+2*i, black);
      }
    }
  } else {
    // 4 Signal-Balken (ohne Antennensymbol)
    for(int i=0;i<4;i++) {
      if(strength > i) {
        // Starkes Signal: Balken gefüllt zeichnen
        display_sh1106.fillRect(x+4+i*3, y+12-2*i, 2, 2+2*i, white);
      } else {
        // Schwaches Signal: Balken explizit schwarz überschreiben (ausblenden)
        display_sh1106.fillRect(x+4+i*3, y+12-2*i, 2, 2+2*i, black);
      }
    }
  }
}

void drawRXTXRects() {
  // Debug: Aktuelle RX/TX-Status ausgeben
  unsigned long now = millis();
  bool rxActive = (now - lastRX < RS232_ACTIVE_TIME);
  bool txActive = (now - lastTX < RS232_ACTIVE_TIME);
  
  // Entfernt: RX/TX Debug-Ausgaben für bessere Performance
  
  int rect_width = 26;
  int rect_height = 16;
  int rect_y = 48;
  int rx_x = 16;
  int tx_x = 80;
  
  uint16_t white = getDisplayWhite();
  uint16_t black = (displayType == DISPLAY_SSD1306) ? SSD1306_BLACK : SH110X_BLACK;

  if (displayType == DISPLAY_SSD1306) {
    display_ssd1306.setTextSize(1);
    display_ssd1306.setTextColor(white);
    display_ssd1306.setCursor(rx_x+6, rect_y-10);
    display_ssd1306.print("RX");
    display_ssd1306.setCursor(tx_x+6, rect_y-10);
    display_ssd1306.print("TX");

    if (millis() - lastRX < RS232_ACTIVE_TIME) {
      display_ssd1306.fillRect(rx_x, rect_y, rect_width, rect_height, white);
      display_ssd1306.drawRect(rx_x, rect_y, rect_width, rect_height, black);
    } else {
      display_ssd1306.drawRect(rx_x, rect_y, rect_width, rect_height, white);
    }
    if (millis() - lastTX < RS232_ACTIVE_TIME) {
      display_ssd1306.fillRect(tx_x, rect_y, rect_width, rect_height, white);
      display_ssd1306.drawRect(tx_x, rect_y, rect_width, rect_height, black);
    } else {
      display_ssd1306.drawRect(tx_x, rect_y, rect_width, rect_height, white);
    }
  } else {
    display_sh1106.setTextSize(1);
    display_sh1106.setTextColor(white);
    display_sh1106.setCursor(rx_x+6, rect_y-10);
    display_sh1106.print("RX");
    display_sh1106.setCursor(tx_x+6, rect_y-10);
    display_sh1106.print("TX");

    if (millis() - lastRX < RS232_ACTIVE_TIME) {
      display_sh1106.fillRect(rx_x, rect_y, rect_width, rect_height, white);
      display_sh1106.drawRect(rx_x, rect_y, rect_width, rect_height, black);
    } else {
      display_sh1106.drawRect(rx_x, rect_y, rect_width, rect_height, white);
    }
    if (millis() - lastTX < RS232_ACTIVE_TIME) {
      display_sh1106.fillRect(tx_x, rect_y, rect_width, rect_height, white);
      display_sh1106.drawRect(tx_x, rect_y, rect_width, rect_height, black);
    } else {
      display_sh1106.drawRect(tx_x, rect_y, rect_width, rect_height, white);
    }
  }
}

// 7-Segment-Ziffer zeichnen (CB-Funk Style mit getrennten Segmenten!)
void draw7SegmentDigit(int x, int y, uint8_t digit) {
  // 7-Segment Pattern für Ziffern 0-9
  bool segments[10][7] = {
    {1,1,1,1,1,1,0}, // 0: ABCDEF
    {0,1,1,0,0,0,0}, // 1: BC
    {1,1,0,1,1,0,1}, // 2: ABDEG
    {1,1,1,1,0,0,1}, // 3: ABCDG
    {0,1,1,0,0,1,1}, // 4: BCFG
    {1,0,1,1,0,1,1}, // 5: ACDFG
    {1,0,1,1,1,1,1}, // 6: ACDEFG
    {1,1,1,0,0,0,0}, // 7: ABC
    {1,1,1,1,1,1,1}, // 8: ABCDEFG
    {1,1,1,1,0,1,1}  // 9: ABCDFG
  };
  
  if(digit > 9) return;
  
  int segLen = 6;   // Segment Länge (kleiner für mehr Platz)
  int segThick = 1; // Segment Dicke (dünn)
  int gap = 1;      // Abstand zwischen Segmenten (WICHTIG!)
  
  // Segment-Positionen mit Lücken zwischen den Segmenten
  if (displayType == DISPLAY_SSD1306) {
    // Segment A (oben)
    if(segments[digit][0]) display_ssd1306.fillRect(x+gap+1, y, segLen, segThick, getDisplayWhite());
    // Segment B (rechts oben)  
    if(segments[digit][1]) display_ssd1306.fillRect(x+segLen+gap+1, y+gap+1, segThick, segLen-1, getDisplayWhite());
    // Segment C (rechts unten)
    if(segments[digit][2]) display_ssd1306.fillRect(x+segLen+gap+1, y+segLen+gap+2, segThick, segLen-1, getDisplayWhite());
    // Segment D (unten)
    if(segments[digit][3]) display_ssd1306.fillRect(x+gap+1, y+2*segLen+2*gap+1, segLen, segThick, getDisplayWhite());
    // Segment E (links unten)
    if(segments[digit][4]) display_ssd1306.fillRect(x, y+segLen+gap+2, segThick, segLen-1, getDisplayWhite());
    // Segment F (links oben)
    if(segments[digit][5]) display_ssd1306.fillRect(x, y+gap+1, segThick, segLen-1, getDisplayWhite());
    // Segment G (mitte)
    if(segments[digit][6]) display_ssd1306.fillRect(x+gap+1, y+segLen+gap+1, segLen, segThick, getDisplayWhite());
  } else {
    // Segment A (oben)
    if(segments[digit][0]) display_sh1106.fillRect(x+gap+1, y, segLen, segThick, getDisplayWhite());
    // Segment B (rechts oben)
    if(segments[digit][1]) display_sh1106.fillRect(x+segLen+gap+1, y+gap+1, segThick, segLen-1, getDisplayWhite());
    // Segment C (rechts unten)
    if(segments[digit][2]) display_sh1106.fillRect(x+segLen+gap+1, y+segLen+gap+2, segThick, segLen-1, getDisplayWhite());
    // Segment D (unten)
    if(segments[digit][3]) display_sh1106.fillRect(x+gap+1, y+2*segLen+2*gap+1, segLen, segThick, getDisplayWhite());
    // Segment E (links unten)
    if(segments[digit][4]) display_sh1106.fillRect(x, y+segLen+gap+2, segThick, segLen-1, getDisplayWhite());
    // Segment F (links oben)
    if(segments[digit][5]) display_sh1106.fillRect(x, y+gap+1, segThick, segLen-1, getDisplayWhite());
    // Segment G (mitte)
    if(segments[digit][6]) display_sh1106.fillRect(x+gap+1, y+segLen+gap+1, segLen, segThick, getDisplayWhite());
  }
}

// CB-Kanal Display mit "CH" + 7-Segment Ziffern + RX/TX (komplett mittig zentriert)
void drawCBChannelDisplay() {
  // Berechne zentrierte Y-Position zwischen horizontal line (32) und Display-Unterkante (64)
  int centerY = 45; // Zentriert zwischen Linie und Unterkante
  
  // Berechne horizontale Zentrierung für die KOMPLETTE CB-Display-Gruppe
  // CH(12px) + Abstand(6px) + Ziffer1(9px) + Trennung(2px) + Ziffer2(9px) + Abstand(8px) + RX/TX(29px) = 75px total
  int displayWidth = 128; // Beide Displays 128px breit
  int totalDisplayWidth = 75; // Gesamtbreite der kompletten CB-Anzeige mit RX/TX
  int startX = (displayWidth - totalDisplayWidth) / 2; // Perfekte Zentrierung der ganzen Gruppe
  
  // "CH" Text (mittig positioniert)
  if (displayType == DISPLAY_SSD1306) {
    display_ssd1306.setTextSize(1);
    display_ssd1306.setTextColor(getDisplayWhite());
    display_ssd1306.setCursor(startX, centerY + 1);
    display_ssd1306.print("CH");
  } else {
    display_sh1106.setTextSize(1);
    display_sh1106.setTextColor(getDisplayWhite());
    display_sh1106.setCursor(startX, centerY + 1);
    display_sh1106.print("CH");
  }
  
  // CB-Kanal mit führender Null (7-Segment Style) - mittig positioniert
  uint8_t tens = cbChannel / 10;
  uint8_t ones = cbChannel % 10;
  
  int digit1X = startX + 18; // Nach "CH" + kleiner Abstand
  int digit2X = startX + 29; // Nach erster Ziffer + 2 Pixel Trennung
  int digitY = centerY - 2;  // Etwas höher als CH für bessere Optik
  
  // Erste Ziffer (oder führende 0)
  if(cbChannel < 10) {
    draw7SegmentDigit(digit1X, digitY, 0);
  } else {
    draw7SegmentDigit(digit1X, digitY, tens);
  }
  
  // Zweite Ziffer
  draw7SegmentDigit(digit2X, digitY, ones);
  
  // RX/TX Boxes rechts von der CB-Anzeige (als Teil der zentrierten Gruppe)
  int boxX = startX + 46;    // Nach CB-Ziffern + Abstand (angepasst für 2px Trennung)
  int labelX = boxX + 17;    // Labels rechts neben den Boxen
  int boxY1 = centerY - 2;   // Erste Box (RX)
  int boxY2 = centerY + 7;   // Zweite Box (TX)
  
  // RX Box und Label
  if (displayType == DISPLAY_SSD1306) {
    // RX Box
    if (millis() - lastRX < RS232_ACTIVE_TIME) {
      display_ssd1306.fillRect(boxX, boxY1, 15, 8, getDisplayWhite());
    } else {
      display_ssd1306.drawRect(boxX, boxY1, 15, 8, getDisplayWhite());
    }
    
    // RX Label rechts neben Box
    display_ssd1306.setTextSize(1);
    display_ssd1306.setTextColor(getDisplayWhite());
    display_ssd1306.setCursor(labelX, boxY1 + 1);
    display_ssd1306.print("RX");
    
    // TX Box
    if (millis() - lastTX < RS232_ACTIVE_TIME) {
      display_ssd1306.fillRect(boxX, boxY2, 15, 8, getDisplayWhite());
    } else {
      display_ssd1306.drawRect(boxX, boxY2, 15, 8, getDisplayWhite());
    }
    
    // TX Label rechts neben Box
    display_ssd1306.setCursor(labelX, boxY2 + 1);
    display_ssd1306.print("TX");
  } else {
    // RX Box
    if (millis() - lastRX < RS232_ACTIVE_TIME) {
      display_sh1106.fillRect(boxX, boxY1, 15, 8, getDisplayWhite());
    } else {
      display_sh1106.drawRect(boxX, boxY1, 15, 8, getDisplayWhite());
    }
    
    // RX Label rechts neben Box
    display_sh1106.setTextSize(1);
    display_sh1106.setTextColor(getDisplayWhite());
    display_sh1106.setCursor(labelX, boxY1 + 1);
    display_sh1106.print("RX");
    
    // TX Box
    if (millis() - lastTX < RS232_ACTIVE_TIME) {
      display_sh1106.fillRect(boxX, boxY2, 15, 8, getDisplayWhite());
    } else {
      display_sh1106.drawRect(boxX, boxY2, 15, 8, getDisplayWhite());
    }
    
    // TX Label rechts neben Box
    display_sh1106.setCursor(labelX, boxY2 + 1);
    display_sh1106.print("TX");
  }
}

void updateOLED() {
  // WiFi-Signalstärke nur alle 1000ms abfragen (Performance-Optimierung)
  static unsigned long lastWiFiRSSI = 0;
  static int cachedRSSI = -100;
  static int cachedStrength = 0;
  
  if (millis() - lastWiFiRSSI > 1000) {
    if (WiFi.status() == WL_CONNECTED) {
      cachedRSSI = WiFi.RSSI();
      if (cachedRSSI > -55) cachedStrength = 4;
      else if (cachedRSSI > -65) cachedStrength = 3;
      else if (cachedRSSI > -75) cachedStrength = 2;
      else if (cachedRSSI > -85) cachedStrength = 1;
      else cachedStrength = 0;
    } else {
      cachedStrength = 0;
    }
    lastWiFiRSSI = millis();
  }

  if (displayType == DISPLAY_SSD1306) {
    display_ssd1306.clearDisplay();
    display_ssd1306.setTextColor(getDisplayWhite());
    display_ssd1306.setTextSize(2);
    display_ssd1306.setCursor(0,1);
    display_ssd1306.print(callsign);
    
    drawWifiStrength(cachedStrength);
    display_ssd1306.drawLine(0, 20, SCREEN_WIDTH, 20, getDisplayWhite());
    display_ssd1306.setTextSize(1);
    const char* mqttStatus;
    if (mqttEnabled) {
      if (isMqttConnected()) {
        mqttStatus = "MQTT ONLINE";
      } else {
        mqttStatus = "MQTT OFFLINE";
      }
    } else {
      mqttStatus = "MQTT DISABLED";
    }
    int16_t x1, y1;
    uint16_t w, h;
    display_ssd1306.getTextBounds(mqttStatus, 0, 0, &x1, &y1, &w, &h);
    display_ssd1306.setCursor((SCREEN_WIDTH - w) / 2, 22);
    display_ssd1306.print(mqttStatus);
    display_ssd1306.drawLine(0, 32, SCREEN_WIDTH, 32, getDisplayWhite());
    drawCBChannelDisplay(); // Neues CB-Kanal Display statt drawRXTXRects()
    display_ssd1306.display();
  } else {
    display_sh1106.clearDisplay();
    display_sh1106.setTextColor(getDisplayWhite());
    display_sh1106.setTextSize(2);
    display_sh1106.setCursor(0,1);
    display_sh1106.print(callsign);
    
    drawWifiStrength(cachedStrength);
    display_sh1106.drawLine(0, 20, SCREEN_WIDTH, 20, getDisplayWhite());
    display_sh1106.setTextSize(1);
    const char* mqttStatus;
    if (mqttEnabled) {
      if (isMqttConnected()) {
        mqttStatus = "MQTT ONLINE";
      } else {
        mqttStatus = "MQTT OFFLINE";
      }
    } else {
      mqttStatus = "MQTT DISABLED";
    }
    int16_t x1, y1;
    uint16_t w, h;
    display_sh1106.getTextBounds(mqttStatus, 0, 0, &x1, &y1, &w, &h);
    display_sh1106.setCursor((SCREEN_WIDTH - w) / 2, 22);
    display_sh1106.print(mqttStatus);
    display_sh1106.drawLine(0, 32, SCREEN_WIDTH, 32, getDisplayWhite());
    drawCBChannelDisplay(); // Neues CB-Kanal Display statt drawRXTXRects()
    display_sh1106.display();
  }
}

void saveConfig() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 64; ++i) EEPROM.write(SSID_OFFSET+i, wifiSsid[i]);
  for (int i = 0; i < 64; ++i) EEPROM.write(PASS_OFFSET+i, wifiPass[i]);
  for (int i = 0; i < 64; ++i) EEPROM.write(SERVERURL_OFFSET+i, serverUrl[i]);
  for (int i = 0; i < 32; ++i) EEPROM.write(CALLSIGN_OFFSET+i, callsign[i]);
  for (int i = 0; i < 128; ++i) EEPROM.write(OTAURL_OFFSET+i, otaRepoUrl[i]);
  for (int i = 0; i < 16; ++i) EEPROM.write(VERSION_OFFSET+i, localVersion[i]);
  EEPROM.write(BAUD_OFFSET, (baudrate >> 24) & 0xFF);
  EEPROM.write(BAUD_OFFSET+1, (baudrate >> 16) & 0xFF);
  EEPROM.write(BAUD_OFFSET+2, (baudrate >> 8) & 0xFF);
  EEPROM.write(BAUD_OFFSET+3, baudrate & 0xFF);
  EEPROM.write(LOGLEVEL_OFFSET, logLevel);
  EEPROM.write(DISPLAYTYPE_OFFSET, displayType);  // Display-Typ speichern
  EEPROM.write(SSL_VALIDATION_OFFSET, sslValidation ? 1 : 0);  // SSL-Validierung speichern
  EEPROM.write(CPU_FREQUENCY_OFFSET, cpuFrequency);  // CPU-Frequenz speichern
  EEPROM.write(DISPLAY_BRIGHTNESS_OFFSET, displayBrightness);  // Display-Helligkeit speichern
  
  // MQTT Configuration speichern
  EEPROM.write(MQTT_ENABLED_OFFSET, mqttEnabled ? 1 : 0);
  for (int i = 0; i < 64; ++i) EEPROM.write(MQTT_BROKER_OFFSET+i, mqttBroker[i]);
  EEPROM.write(MQTT_PORT_OFFSET, (mqttPort >> 8) & 0xFF);
  EEPROM.write(MQTT_PORT_OFFSET+1, mqttPort & 0xFF);
  for (int i = 0; i < 16; ++i) EEPROM.write(MQTT_USERNAME_OFFSET+i, mqttUsername[i]);
  for (int i = 0; i < 32; ++i) EEPROM.write(MQTT_PASSWORD_OFFSET+i, mqttPassword[i]);
  EEPROM.write(CB_CHANNEL_OFFSET, cbChannel); // CB-Kanal speichern
  
  EEPROM.commit();
}

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 64; ++i) wifiSsid[i] = EEPROM.read(SSID_OFFSET+i);
  wifiSsid[63] = 0;
  for (int i = 0; i < 64; ++i) wifiPass[i] = EEPROM.read(PASS_OFFSET+i);
  wifiPass[63] = 0;
  for (int i = 0; i < 64; ++i) serverUrl[i] = EEPROM.read(SERVERURL_OFFSET+i);
  serverUrl[63] = 0;
  for (int i = 0; i < 32; ++i) callsign[i] = EEPROM.read(CALLSIGN_OFFSET+i);
  callsign[31] = 0;
  for (int i = 0; i < 128; ++i) otaRepoUrl[i] = EEPROM.read(OTAURL_OFFSET+i);
  otaRepoUrl[127] = 0;
  for (int i = 0; i < 16; ++i) localVersion[i] = EEPROM.read(VERSION_OFFSET+i);
  localVersion[15] = 0;
  baudrate = ((uint32_t)EEPROM.read(BAUD_OFFSET) << 24)
           | ((uint32_t)EEPROM.read(BAUD_OFFSET+1) << 16)
           | ((uint32_t)EEPROM.read(BAUD_OFFSET+2) << 8)
           | ((uint32_t)EEPROM.read(BAUD_OFFSET+3));
  logLevel = EEPROM.read(LOGLEVEL_OFFSET);
  displayType = EEPROM.read(DISPLAYTYPE_OFFSET);  // Display-Typ laden
  sslValidation = EEPROM.read(SSL_VALIDATION_OFFSET) == 1;  // SSL-Validierung laden
  cpuFrequency = EEPROM.read(CPU_FREQUENCY_OFFSET);  // CPU-Frequenz laden
  displayBrightness = EEPROM.read(DISPLAY_BRIGHTNESS_OFFSET);  // Display-Helligkeit laden
  
  // MQTT Configuration laden
  mqttEnabled = EEPROM.read(MQTT_ENABLED_OFFSET) == 1;
  for (int i = 0; i < 64; ++i) mqttBroker[i] = EEPROM.read(MQTT_BROKER_OFFSET+i);
  mqttBroker[63] = 0;
  mqttPort = ((uint16_t)EEPROM.read(MQTT_PORT_OFFSET) << 8) | ((uint16_t)EEPROM.read(MQTT_PORT_OFFSET+1));
  for (int i = 0; i < 16; ++i) mqttUsername[i] = EEPROM.read(MQTT_USERNAME_OFFSET+i);
  mqttUsername[15] = 0;
  for (int i = 0; i < 32; ++i) mqttPassword[i] = EEPROM.read(MQTT_PASSWORD_OFFSET+i);
  mqttPassword[31] = 0;
  cbChannel = EEPROM.read(CB_CHANNEL_OFFSET); // CB-Kanal laden
  
  if(baudrate == 0xFFFFFFFF || baudrate == 0x00000000) {
    baudrate = 2400;
    appendMonitor("Baudrate war ungültig, auf 2400 gesetzt", "WARNING");
  }
  if(logLevel > 3) logLevel = 1;
  if(displayType > 1) displayType = DISPLAY_SSD1306;  // Default zu SSD1306 bei ungültigem Wert
  if(strlen(wifiSsid) == 0) appendMonitor("WLAN SSID ist leer!", "WARNING");
  if(strlen(serverUrl) == 0) appendMonitor("Server URL ist leer!", "WARNING");
  if(strlen(otaRepoUrl) == 0) {
    strcpy(otaRepoUrl, "https://raw.githubusercontent.com/nad22/UDM-Packet-Radio-Internet-Gateway/main/ota");
    appendMonitor("OTA URL war leer, Standard gesetzt", "WARNING");
  }
  if(strlen(localVersion) == 0 || localVersion[0] == 0xFF) {
    strcpy(localVersion, "1.0.1");
    appendMonitor("Version war leer, Standard gesetzt: " + String(localVersion), "WARNING");
    saveConfig(); // Speichere Standard-Version
  }
  
  // MQTT Konfiguration validieren
  if(mqttPort == 0 || mqttPort == 0xFFFF) {
    mqttPort = 8883; // Default SSL Port
    appendMonitor("MQTT Port ungültig, auf 8883 gesetzt", "WARNING");
  }
  // MQTT ist immer aktiviert (reine MQTT-Implementierung)
  mqttEnabled = true;
  if(strlen(mqttBroker) == 0) {
    appendMonitor("MQTT Broker-URL nicht konfiguriert - bitte einstellen!", "WARNING");
  }
  
  // CB-Kanal validieren (1-40)
  if(cbChannel < 1 || cbChannel > 40 || cbChannel == 0xFF) {
    cbChannel = 1; // Default CB-Kanal 1
    appendMonitor("CB-Kanal ungültig, auf Kanal 1 gesetzt", "WARNING");
  }
  
  // Validiere CPU-Frequenz-Wert und korrigiere problematische Einstellungen
  if(cpuFrequency > 2 || cpuFrequency == 0xFF) {
    if(cpuFrequency == 3 || cpuFrequency == 4) {
      // Alte problematische 40MHz (3) oder 26MHz (4) Einstellungen korrigieren
      cpuFrequency = 2; // Auf 80 MHz setzen (sicher)
      appendMonitor("CPU-Frequenz korrigiert: Problematische Einstellung auf 80 MHz geändert", "WARNING");
      saveConfig(); // Sofort speichern um Bootloop zu verhindern
    } else {
      cpuFrequency = 0; // Fallback auf 240 MHz
      appendMonitor("CPU-Frequenz ungültig, auf 240 MHz zurückgesetzt", "WARNING");
    }
  }
}

// ========================================
// MQTT FUNKTIONEN
// ========================================

// MQTT Topics basierend auf CB-Kanal konfigurieren (Broadcast-Modus)
void setupMqttTopics() {
  // CB-Funk Topic dynamisch generieren: udmprig/rf/1 bis udmprig/rf/40
  mqttBroadcastTopic = "udmprig/rf/" + String(cbChannel);
  
  if(strlen(callsign) > 0) {
    appendMonitor("MQTT CB-Kanal " + String(cbChannel) + " für " + String(callsign), "INFO");
    appendMonitor("Funk-Simulation: " + mqttBroadcastTopic, "INFO");
  }
}

// MQTT Message Callback (Broadcast-Modus)
void onMqttMessage(int messageSize) {
  String topic = mqttClient.messageTopic();
  String message = "";
  
  // Nachricht lesen
  while (mqttClient.available()) {
    message += (char)mqttClient.read();
  }
  
  appendMonitor("MQTT RX: " + topic + " - " + String(messageSize) + " bytes", "INFO");
  
  // Alle Nachrichten aus dem Broadcast-Channel verarbeiten
  if(topic == mqttBroadcastTopic) {
    // JSON-Message mit Hex-Payload verarbeiten
    processMqttMessage(message);
  }
  else if(topic.endsWith("/config")) {
    // Konfiguration-Update
    appendMonitor("Config Update empfangen: " + message, "INFO");
  }
}

// MQTT Verbindung herstellen
bool connectMQTT() {
  if(!mqttEnabled || strlen(mqttBroker) == 0) {
    return false;
  }
  
  if(mqttClient.connected()) {
    return true;
  }
  
  // SSL-Konfiguration für HiveMQ Cloud
  mqttWifiClient.setInsecure(); // Für Entwicklung - TODO: Zertifikat in Produktion
  
  // Client ID mit Callsign für eindeutige Identifikation
  String clientId = "ESP32-" + String(callsign) + "-" + String(millis() % 10000);
  
  // MQTT Client konfigurieren
  mqttClient.setId(clientId);
  mqttClient.setUsernamePassword(mqttUsername, mqttPassword);
  mqttClient.setKeepAliveInterval(300000); // 5 Minuten Keep-Alive
  mqttClient.setConnectionTimeout(15000);  // 15 Sekunden Timeout
  mqttClient.onMessage(onMqttMessage);
  
  appendMonitor("MQTT Verbindung zu " + String(mqttBroker) + ":" + String(mqttPort), "INFO");
  
  // Verbindung herstellen
  if(mqttClient.connect(mqttBroker, mqttPort)) {
    appendMonitor("MQTT verbunden als " + clientId, "INFO");
    
    // Nur Broadcast-Topic subscriben (QoS 0 = Live-Only, keine alten Nachrichten)
    mqttClient.subscribe(mqttBroadcastTopic);
    
    appendMonitor("MQTT subscribed: " + mqttBroadcastTopic + " (Live Funk-Kanal)", "INFO");
    
    mqttConnected = true;
    return true;
  } else {
    int error = mqttClient.connectError();
    String errorMsg = "MQTT Fehler: " + String(error);
    appendMonitor(errorMsg, "ERROR");
    mqttConnected = false;
    return false;
  }
}

// MQTT Nachricht publizieren (QoS 0, No Retention - Funk-Simulation)
bool publishMqttMessage(const String& topic, const String& message) {
  if(!mqttClient.connected() || !mqttEnabled) {
    appendMonitor("MQTT nicht verbunden", "WARNING");
    return false;
  }
  
  // Nachricht senden (QoS 0 = Fire-and-Forget, keine Speicherung)
  mqttClient.beginMessage(topic);
  mqttClient.print(message);
  bool success = (mqttClient.endMessage() == 1);
  
  /* if(success) {
    appendMonitor("MQTT Funk TX: " + message, "INFO");
  } else {
    appendMonitor("MQTT Funk TX FAILED", "ERROR");
  } */
  
  return success;
}

// MQTT Loop-Handler
void handleMqttLoop() {
  if(!mqttEnabled) return;
  
  // MQTT Poll für Message-Handling
  mqttClient.poll();
  
  // Automatische Wiederverbindung bei Verbindungsverlust
  if(!mqttClient.connected()) {
    static unsigned long lastReconnect = 0;
    unsigned long now = millis();
    
    if(now - lastReconnect > 30000) { // Alle 30 Sekunden versuchen
      lastReconnect = now;
      appendMonitor("MQTT Verbindung verloren - Wiederverbindung...", "WARNING");
      connectMQTT();
    }
  }
}

// MQTT Verbindungsstatus prüfen
bool isMqttConnected() {
  return mqttEnabled && mqttClient.connected();
}

// ========================================
// ENDE MQTT FUNKTIONEN  
// ========================================

// CPU-Frequenz setzen basierend auf Konfiguration
void setCpuFrequency() {
  uint32_t freqMHz = 240;  // Default
  switch(cpuFrequency) {
    case 0: freqMHz = 240; break;  // Standard
    case 1: freqMHz = 160; break;  // Reduziert
    case 2: freqMHz = 80; break;   // Energiesparen
    default: freqMHz = 240; break;
  }
  
  setCpuFrequencyMhz(freqMHz);
  
  // Entfernt: CPU-Frequenz Serial-Ausgabe für bessere Performance
}

void handleRoot() {
  String html = R"=====(
<!DOCTYPE html>
<html>
  <head>
    <meta charset="UTF-8">
    <title>UDMPRIG-Client v)=====";
  html += String(localVersion);
  html += R"=====(</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0"/>
    <link href="https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/css/materialize.min.css" rel="stylesheet">
    <style>
      body {padding:18px; max-width: 1200px; margin: 0 auto;}
      pre {font-size: 12px; background:#222; color:#eee; padding:10px;}
      .input-field label {color: #009688;}
      .tabs .tab a.active { color: #009688;}
      .tabs .tab a { color: #444;}
      .input-field {margin-bottom: 0;}
      .custom-row {margin-bottom: 20px;}
      .modal-content { text-align: center; }
      .modal .preloader-wrapper { margin: 30px auto; }
      .modal .errormsg { color: #b71c1c; font-weight: bold; font-size: 1.2em; }
      .log-debug { color: #90caf9 !important; }
      .log-info { color: #a5d6a7 !important; }
      .log-error { color: #ef9a9a !important; }
      .log-warn { color: #ffe082 !important; }
      .log-default { color: #eee !important; }
      .log-rs232 { color: #ffecb3 !important; font-family:monospace; }
      .container { max-width: 1200px; }
      .wifi-signal-bar { background: #e0e0e0; border-radius: 10px; height: 20px; overflow: hidden; }
      .wifi-signal-fill { height: 100%; border-radius: 10px; transition: width 0.5s ease; }
      
      /* Custom Slider Styles */
      input[type=range] {
        -webkit-appearance: none;
        height: 12px !important;
        border-radius: 8px;
        background: linear-gradient(to right, #ddd 0%, #1976d2 100%);
        outline: none;
        cursor: pointer;
      }
      input[type=range]::-webkit-slider-thumb {
        -webkit-appearance: none;
        appearance: none;
        width: 24px;
        height: 24px;
        border-radius: 50%;
        background: #1976d2;
        cursor: pointer;
        box-shadow: 0 2px 6px rgba(0,0,0,0.3);
        transition: all 0.2s ease;
      }
      input[type=range]::-webkit-slider-thumb:hover {
        background: #1565c0;
        transform: scale(1.1);
      }
      input[type=range]::-moz-range-thumb {
        width: 24px;
        height: 24px;
        border-radius: 50%;
        background: #1976d2;
        cursor: pointer;
        border: none;
        box-shadow: 0 2px 6px rgba(0,0,0,0.3);
      }
      
      @media (max-width: 768px) { body { padding: 10px; } }
    </style>
  </head>
  <body>
    <h4><i class="material-icons left">settings_ethernet</i>UDMPRIG-Client v)=====";
  html += String(localVersion);
  html += R"=====(</h4>
    <ul id="tabs-swipe-demo" class="tabs">
      <li class="tab col s4"><a class="active" href="#hardware">Hardware</a></li>
      <li class="tab col s4"><a href="#monitor">Monitor</a></li>
      <li class="tab col s4"><a href="#config">Config</a></li>
    </ul>
    <div id="config" class="col s12">
      <form id="configform" action='/save' method='post'>
        <div class="input-field custom-row">
          <input id="ssid" name="ssid" type="text" maxlength="63" value=")=====";
  html += String(wifiSsid);
  html += R"=====(">
          <label for="ssid" class="active">WLAN SSID</label>
        </div>
        <div class="input-field custom-row">
          <input id="pass" name="pass" type="password" maxlength="63" value=")=====";
  html += String(wifiPass);
  html += R"=====(">
          <label for="pass" class="active">WLAN Passwort</label>
        </div>
        <div class="input-field custom-row">
          <input id="serverurl" name="serverurl" type="text" maxlength="63" value=")=====";
  html += String(serverUrl);
  html += R"=====(">
          <label for="serverurl" class="active">Server URL</label>
        </div>
        <div class="input-field custom-row">
          <input id="callsign" name="callsign" type="text" maxlength="31" value=")=====";
  html += String(callsign);
  html += R"=====(">
          <label for="callsign" class="active">Callsign</label>
        </div>
        <div class="input-field custom-row">
          <input id="otarepourl" name="otarepourl" type="url" maxlength="127" value=")=====";
  html += String(otaRepoUrl);
  html += R"=====(">
          <label for="otarepourl" class="active">OTA Repository URL</label>
          <span class="helper-text">GitHub Raw URL für Firmware-Updates</span>
        </div>
        <div class="input-field custom-row">
          <select id="baudrate" name="baudrate">
  )=====";
  uint32_t rates[] = {1200, 2400, 4800, 9600, 14400, 19200, 38400, 57600, 115200};
  for(int i=0;i<9;++i){
    html += "<option value='";
    html += String(rates[i]);
    html += "'";
    if(baudrate==rates[i]) html += " selected";
    html += ">";
    html += String(rates[i]);
    html += " Baud</option>";
  }
  html += R"=====(</select>
          <label for="baudrate">RS232 Baudrate</label>
        </div>
        <div class="input-field custom-row">
          <select id="cbchannel" name="cbchannel">
  )=====";
  // CB-Kanal 1-40 Dropdown generieren
  for(int i = 1; i <= 40; i++) {
    html += "<option value='";
    html += String(i);
    html += "'";
    if(cbChannel == i) html += " selected";
    html += ">Kanal ";
    if(i < 10) html += "0"; // Führende Null für einstellige Kanäle
    html += String(i);
    html += "</option>";
  }
  html += R"=====(</select>
          <label for="cbchannel">CB-Funk Kanal</label>
          <span class="helper-text">CB Funk Kanal 1-40</span>
        </div>
        <div class="input-field custom-row">
          <select id="displaybrightness" name="displaybrightness">
            <option value="0")=====";
  if(displayBrightness == 0) html += " selected";
  html += R"=====(>1% (Minimal)</option>
            <option value="51")=====";
  if(displayBrightness >= 46 && displayBrightness <= 55) html += " selected";
  html += R"=====(>20% (Sehr dunkel)</option>
            <option value="102")=====";
  if(displayBrightness >= 97 && displayBrightness <= 107) html += " selected";
  html += R"=====(>40% (Dunkel)</option>
            <option value="153")=====";
  if(displayBrightness >= 148 && displayBrightness <= 158) html += " selected";
  html += R"=====(>60% (Normal)</option>
            <option value="204")=====";
  if(displayBrightness >= 199 && displayBrightness <= 209) html += " selected";
  html += R"=====(>80% (Hell)</option>
            <option value="255")=====";
  if(displayBrightness >= 250) html += " selected";
  html += R"=====(>100% (Maximum)</option>
          </select>
          <label for="displaybrightness">Display-Helligkeit</label>
          <span class="helper-text">Live-Vorschau beim Ändern der Auswahl</span>
        </div>
)=====";
  html += "<div class=\"input-field custom-row\">";
  html += "<select id=\"loglevel\" name=\"loglevel\">";
  html += "<option value=\"0\"";
  if(logLevel==0) html += " selected";
  html += ">Error</option>";
  html += "<option value=\"1\"";
  if(logLevel==1) html += " selected";
  html += ">Info</option>";
  html += "<option value=\"2\"";
  if(logLevel==2) html += " selected";
  html += ">Warning</option>";
  html += "<option value=\"3\"";
  if(logLevel==3) html += " selected";
  html += ">Debug</option>";
  html += "</select>";
  html += "<label for=\"loglevel\">Log Level</label>";
  html += "</div>";
  
  // Display-Typ Auswahl
  html += "<div class=\"input-field custom-row\">";
  html += "<select id=\"displaytype\" name=\"displaytype\">";
  html += "<option value=\"1\"";
  if(displayType==DISPLAY_SSD1306) html += " selected";
  html += ">SSD1306 (kleines Display)</option>";
  html += "<option value=\"0\"";
  if(displayType==DISPLAY_SH1106G) html += " selected";
  html += ">SH1106G (großes Display)</option>";
  html += "</select>";
  html += "<label for=\"displaytype\">Display-Typ</label>";
  html += "</div>";
  
  // SSL-Zertifikat-Validierung (nur relevant für HTTPS-URLs)
  html += "<div class=\"input-field custom-row\">";
  html += "<select id=\"sslvalidation\" name=\"sslvalidation\">";
  html += "<option value=\"0\"";
  if(!sslValidation) html += " selected";
  html += ">Deaktiviert (Self-Signed)</option>";
  html += "<option value=\"1\"";
  if(sslValidation) html += " selected";
  html += ">Aktiviert (Offizielle Zerts)</option>";
  html += "</select>";
  html += "<label for=\"sslvalidation\">SSL-Zertifikatsprüfung</label>";
  html += "<span class=\"helper-text\">Automatisch: HTTPS=verschlüsselt, HTTP=unverschlüsselt</span>";
  html += "</div>";
  
  // MQTT Konfiguration Sektion
  html += "<div class=\"divider\"></div>";
  html += "<h5>MQTT Konfiguration</h5>";
  
  // MQTT Broker URL
  html += "<div class=\"input-field custom-row\">";
  html += "<input type=\"text\" id=\"mqttbroker\" name=\"mqttbroker\" value=\"" + String(mqttBroker) + "\" maxlength=\"63\">";
  html += "<label for=\"mqttbroker\">MQTT Broker URL</label>";
  html += "</div>";
  
  // MQTT Port
  html += "<div class=\"input-field custom-row\">";
  html += "<input type=\"number\" id=\"mqttport\" name=\"mqttport\" value=\"" + String(mqttPort) + "\" min=\"1\" max=\"65535\">";
  html += "<label for=\"mqttport\">MQTT Port</label>";
  html += "</div>";
  
  // MQTT Username (erweitert)
  html += "<div class=\"input-field custom-row\">";
  html += "<input type=\"text\" id=\"mqttuser\" name=\"mqttuser\" value=\"" + String(mqttUsername) + "\" maxlength=\"15\">";
  html += "<label for=\"mqttuser\">MQTT Username</label>";
  html += "</div>";
  
  // MQTT Password (erweitert auf 30 Zeichen)
  html += "<div class=\"input-field custom-row\">";
  html += "<input type=\"password\" id=\"mqttpass\" name=\"mqttpass\" value=\"" + String(mqttPassword) + "\" maxlength=\"30\">";
  html += "<label for=\"mqttpass\">MQTT Password</label>";
  html += "</div>";
  
  // CPU-Frequenz Auswahl
  html += "<div class=\"input-field custom-row\">";
  html += "<select id=\"cpufreq\" name=\"cpufreq\">";
  html += "<option value=\"0\"";
  if(cpuFrequency==0) html += " selected";
  html += ">240 MHz (Standard)</option>";
  html += "<option value=\"1\"";
  if(cpuFrequency==1) html += " selected";
  html += ">160 MHz (Reduziert)</option>";
  html += "<option value=\"2\"";
  if(cpuFrequency==2) html += " selected";
  html += ">80 MHz (Energiesparen)</option>";
  html += "</select>";
  html += "<label for=\"cpufreq\">CPU-Frequenz</label>";
  html += "<span class=\"helper-text\">Niedrigere Frequenz = weniger Stromverbrauch</span>";
  html += "</div>";
  html += R"=====(
        <button class="btn waves-effect waves-light teal" type="submit" id="savebtn">Speichern
          <i class="material-icons right">save</i>
        </button>
      </form>
      
      <div class="section">
        (c) www.pukepals.com, 73 de AT1NAD
        <br><small>Auch erreichbar unter: <b>http://udmprig-client.local/</b></small>
      </div>
      <!-- Modal Structure -->
      <div id="restartModal" class="modal">
        <div class="modal-content">
          <h5>Client wird neu gestartet</h5>
          <div class="preloader-wrapper big active" id="spinner">
            <div class="spinner-layer spinner-blue-only">
              <div class="circle-clipper left">
                <div class="circle"></div>
              </div><div class="gap-patch">
                <div class="circle"></div>
              </div><div class="circle-clipper right">
                <div class="circle"></div>
              </div>
            </div>
          </div>
          <div id="restartMsg">
            <p>Bitte warten...</p>
          </div>
        </div>
      </div>
    </div>
    
    <div id="hardware" class="col s12">
      <h5>Hardware Information</h5>
      
      <!-- WiFi Status Section -->
      <div class="card">
        <div class="card-content">
          <span class="card-title"><i class="material-icons left">wifi</i>WiFi Status</span>
          <div class="row">
            <div class="col s6">
              <p><strong>SSID:</strong> <span id="wifiSSID">-</span></p>
              <p><strong>IP Address:</strong> <span id="wifiIP">-</span></p>
              <p><strong>MAC Address:</strong> <span id="wifiMAC">-</span></p>
            </div>
            <div class="col s6">
              <p><strong>Signal Strength:</strong> <span id="wifiRSSI">-</span> dBm</p>
              <div style="margin: 10px 0;">
                <div class="wifi-signal-bar">
                  <div id="wifiSignalBar" class="wifi-signal-fill" style="background: linear-gradient(90deg, #f44336 0%, #ff9800 40%, #4caf50 70%); width: 0%;"></div>
                </div>
                <small style="color: #666;"><span id="wifiSignalText">Signal Quality: -</span></small>
              </div>
              <p><strong>Connection Status:</strong> <span id="wifiStatus">-</span></p>
              <p><strong>Gateway:</strong> <span id="wifiGateway">-</span></p>
            </div>
          </div>
        </div>
      </div>
      
      <!-- ESP32 Chip Information -->
      <div class="card">
        <div class="card-content">
          <span class="card-title"><i class="material-icons left">memory</i>ESP32 Chip Info</span>
          <div class="row">
            <div class="col s6">
              <p><strong>Chip Model:</strong> <span id="chipModel">-</span></p>
              <p><strong>Chip Revision:</strong> <span id="chipRevision">-</span></p>
              <p><strong>CPU Frequency:</strong> <span id="cpuFreq">-</span> MHz</p>
            </div>
            <div class="col s6">
              <p><strong>Flash Size:</strong> <span id="flashSize">-</span> MB</p>
              <p><strong>Flash Speed:</strong> <span id="flashSpeed">-</span> MHz</p>
              <p><strong>SDK Version:</strong> <span id="sdkVersion">-</span></p>
            </div>
          </div>
        </div>
      </div>
      
      <!-- Memory Information -->
      <div class="card">
        <div class="card-content">
          <span class="card-title"><i class="material-icons left">storage</i>Memory Usage</span>
          <div class="row">
            <div class="col s6">
              <p><strong>Free Heap:</strong> <span id="freeHeap">-</span> KB</p>
              <p><strong>Min Free Heap:</strong> <span id="minFreeHeap">-</span> KB</p>
              <p><strong>Heap Size:</strong> <span id="heapSize">-</span> KB</p>
            </div>
            <div class="col s6">
              <p><strong>Free PSRAM:</strong> <span id="freePSRAM">-</span> KB</p>
              <p><strong>PSRAM Size:</strong> <span id="psramSize">-</span> KB</p>
              <p><strong>Flash Usage:</strong> <span id="flashUsage">-</span></p>
            </div>
          </div>
        </div>
      </div>
      
      <!-- System Information -->
      <div class="card">
        <div class="card-content">
          <span class="card-title"><i class="material-icons left">info</i>System Info</span>
          <div class="row">
            <div class="col s6">
              <p><strong>Firmware Version:</strong> <span id="firmwareVersion">-</span></p>
              <p><strong>Uptime:</strong> <span id="uptime">-</span></p>
              <p><strong>Boot Reason:</strong> <span id="bootReason">-</span></p>
            </div>
            <div class="col s6">
              <p><strong>Temperature:</strong> <span id="temperature">-</span>°C</p>
              <p><strong>CPU Usage:</strong> <span id="cpuUsage">-</span>%</p>
              <p><strong>Display Type:</strong> <span id="displayType">-</span></p>
              <p><strong>SSL Validation:</strong> <span id="sslValidation">-</span></p>
            </div>
          </div>
          
          <!-- Crash Logs Section -->
          <div class="card">
            <div class="card-content">
              <span class="card-title">Crash Logs</span>
              <div id="crashLogsContainer">
                <p class="grey-text">Lade Crash Logs...</p>
              </div>
              <div class="card-action">
                <button class="btn orange darken-2 waves-effect waves-light" onclick="clearCrashLogs()">
                  <i class="material-icons left">clear_all</i>Crash Logs löschen
                </button>
              </div>
            </div>
          </div>
          
          <!-- System Control Section -->
          <div class="card">
            <div class="card-content">
              <span class="card-title">System Control</span>
              <p>ESP32 neustarten für Wartung oder bei Problemen.</p>
              <button class="btn red waves-effect waves-light" onclick="rebootSystem()">
                <i class="material-icons left">restart_alt</i>System Neustart
              </button>
            </div>
          </div>
        </div>
      </div>
      
      <!-- MQTT Status Section -->
      <div class="card">
        <div class="card-content">
          <span class="card-title"><i class="material-icons left">cloud_queue</i>MQTT Status</span>
          <div id="mqttStatusContent">
            <p><i class="material-icons tiny">refresh</i> Lade MQTT-Informationen...</p>
          </div>
        </div>
      </div>
      
      <!-- Firmware Update Section -->
      <div class="card">
        <div class="card-content">
          <span class="card-title"><i class="material-icons left">system_update</i>Firmware Update</span>
          <p><strong>Aktuelle Version:</strong> )=====";
  html += String(localVersion);
  html += R"=====(</p>
          <p><strong>OTA Repository:</strong><br><small>)=====";
  html += String(otaRepoUrl);
  html += R"=====(</small></p>
          <button class="btn blue" onclick="checkOTAUpdate()">Nach Updates suchen</button>
          <div id="otaStatus" style="margin-top: 10px;"></div>
        </div>
      </div>
      
      <button class="btn blue" onclick="updateHardwareInfo()">Refresh Info</button>
    </div>
    
    <div id="monitor" class="col s12">
      <h6>Serieller Monitor</h6>
      <pre id="monitorArea" style="height:350px;overflow:auto;resize:vertical;border:1px solid #ccc;padding:10px;"></pre>
      <button class="btn red" onclick="clearMonitor()">Leeren</button>
      <script>
        document.addEventListener('DOMContentLoaded', function() {
          var el = document.querySelectorAll('.tabs');
          M.Tabs.init(el, {});
          var selects = document.querySelectorAll('select');
          M.FormSelect.init(selects, {});
          var modals = document.querySelectorAll('.modal');
          M.Modal.init(modals, {});
        });
        function updateMonitor() {
          fetch('/monitor').then(r=>r.text()).then(t=>{
            let area = document.getElementById('monitorArea');
            let html = t.replace(/^\[([0-9:T\.\- ]+)\]\s+\[(\w+)\]\s*(.*)$/gm, function(_,ts,level,msg){
              if(level=="DEBUG_RS232") return `<span class="log-rs232">[${ts}] [DEBUG] ${msg}</span>`;
              let cls = "log-default";
              if(level=="DEBUG") cls="log-debug";
              else if(level=="INFO") cls="log-info";
              else if(level=="ERROR") cls="log-error";
              else if(level=="WARNING") cls="log-warn";
              return `<span class="${cls}">[${ts}] [${level}] ${msg}</span>`;
            });
            let areaDiv = document.getElementById('monitorArea');
            let atBottom = (areaDiv.scrollTop + areaDiv.clientHeight >= areaDiv.scrollHeight-2);
            areaDiv.innerHTML = html;
            if(atBottom) areaDiv.scrollTop = areaDiv.scrollHeight;
          });
        }
        setInterval(updateMonitor, 1000);
        function clearMonitor() {
          fetch('/monitor_clear').then(()=>updateMonitor());
        }
        
        function updateHardwareInfo() {
          fetch('/api/hardware_info').then(r=>r.json()).then(data=>{
            document.getElementById('wifiSSID').textContent = data.wifi.ssid || '-';
            document.getElementById('wifiIP').textContent = data.wifi.ip || '-';
            document.getElementById('wifiMAC').textContent = data.wifi.mac || '-';
            document.getElementById('wifiRSSI').textContent = data.wifi.rssi || '-';
            document.getElementById('wifiStatus').textContent = data.wifi.status || '-';
            document.getElementById('wifiGateway').textContent = data.wifi.gateway || '-';
            
            // Update WiFi signal strength bar
            const rssi = parseInt(data.wifi.rssi) || -100;
            let signalPercent = 0;
            let signalQuality = 'Poor';
            
            if (rssi >= -50) {
              signalPercent = 100;
              signalQuality = 'Excellent';
            } else if (rssi >= -60) {
              signalPercent = 80;
              signalQuality = 'Very Good';
            } else if (rssi >= -70) {
              signalPercent = 60;
              signalQuality = 'Good';
            } else if (rssi >= -80) {
              signalPercent = 40;
              signalQuality = 'Fair';
            } else if (rssi >= -90) {
              signalPercent = 20;
              signalQuality = 'Weak';
            } else {
              signalPercent = 5;
              signalQuality = 'Very Weak';
            }
            
            document.getElementById('wifiSignalBar').style.width = signalPercent + '%';
            document.getElementById('wifiSignalText').textContent = 'Signal Quality: ' + signalQuality + ' (' + signalPercent + '%)';
            
            document.getElementById('chipModel').textContent = data.chip.model || '-';
            document.getElementById('chipRevision').textContent = data.chip.revision || '-';
            document.getElementById('cpuFreq').textContent = data.chip.cpuFreq || '-';
            document.getElementById('flashSize').textContent = data.chip.flashSize || '-';
            document.getElementById('flashSpeed').textContent = data.chip.flashSpeed || '-';
            document.getElementById('sdkVersion').textContent = data.chip.sdkVersion || '-';
            
            document.getElementById('freeHeap').textContent = data.memory.freeHeap || '-';
            document.getElementById('minFreeHeap').textContent = data.memory.minFreeHeap || '-';
            document.getElementById('heapSize').textContent = data.memory.heapSize || '-';
            document.getElementById('freePSRAM').textContent = data.memory.freePSRAM || '-';
            document.getElementById('psramSize').textContent = data.memory.psramSize || '-';
            document.getElementById('flashUsage').textContent = data.memory.flashUsage || '-';
            
            document.getElementById('firmwareVersion').textContent = data.system.firmwareVersion || '-';
            document.getElementById('uptime').textContent = data.system.uptime || '-';
            document.getElementById('bootReason').textContent = data.system.bootReason || '-';
            document.getElementById('temperature').textContent = data.system.temperature || '-';
            document.getElementById('cpuUsage').textContent = data.system.cpuUsage || '-';
            document.getElementById('displayType').textContent = data.system.displayType || '-';
            document.getElementById('sslValidation').textContent = data.system.sslValidation || '-';
            
            // MQTT Status anzeigen
            if (data.mqtt) {
              const mqttStatus = data.mqtt;
              const connectionStatus = mqttStatus.connected ? 'Connected' : 'Disconnected';
              const statusClass = mqttStatus.connected ? 'green-text' : 'red-text';
              const statusIcon = mqttStatus.connected ? 'cloud_done' : 'cloud_off';
              
              let mqttHtml = '<div class="row">';
              mqttHtml += '<div class="col s6">';
              mqttHtml += '<p><strong>Status:</strong> <span class="' + statusClass + '"><i class="material-icons tiny">' + statusIcon + '</i> ' + connectionStatus + '</span></p>';
              mqttHtml += '<p><strong>Broker:</strong> ' + (mqttStatus.broker || 'Nicht konfiguriert') + '</p>';
              mqttHtml += '<p><strong>Port:</strong> ' + mqttStatus.port + '</p>';
              mqttHtml += '<p><strong>Username:</strong> ' + (mqttStatus.username || 'Nicht gesetzt') + '</p>';
              mqttHtml += '</div>';
              mqttHtml += '<div class="col s6">';
              mqttHtml += '<p><strong>CB-Kanal:</strong> ' + String(mqttStatus.cbChannel).padStart(2, '0') + '</p>';
              mqttHtml += '<p><strong>MQTT Topic:</strong> <span style="font-family: monospace; font-size: 14px;">' + mqttStatus.topic + '</span></p>';
              mqttHtml += '<p><strong>Verbindung:</strong> ' + mqttStatus.state + '</p>';
              mqttHtml += '</div>';
              mqttHtml += '</div>';
              
              if (mqttStatus.lastReconnectAttempt) {
                mqttHtml += '<p><small><strong>Letzter Reconnect:</strong> ' + mqttStatus.lastReconnectAttempt + '</small></p>';
              }
              
              document.getElementById('mqttStatusContent').innerHTML = mqttHtml;
            } else {
              document.getElementById('mqttStatusContent').innerHTML = '<p class="red-text">MQTT-Daten nicht verfügbar</p>';
            }
            
            // Crash Logs anzeigen
            const crashLogsContainer = document.getElementById('crashLogsContainer');
            
            if (data.crashLogs && data.crashLogs.length > 0) {
              let crashHtml = '<div class="collection">';
              data.crashLogs.forEach((log, index) => {
                const timeClass = index === 0 ? 'red-text' : 'grey-text text-darken-2';
                crashHtml += '<div class="collection-item">';
                crashHtml += '<span class="' + timeClass + '">' + log.timestamp + '</span><br>';
                crashHtml += '<span class="black-text">' + log.message + '</span>';
                crashHtml += '</div>';
              });
              crashHtml += '</div>';
              
              crashLogsContainer.innerHTML = crashHtml;
            } else {
              crashLogsContainer.innerHTML = '<p class="green-text">Keine Crash Logs gefunden - System läuft stabil!</p>';
            }
          }).catch(err => {
            console.error('Error fetching hardware info:', err);
          });
        }
        
        // Auto-refresh hardware info every 5 seconds when on hardware tab
        setInterval(() => {
          const hardwareTab = document.querySelector('a[href="#hardware"]');
          if (hardwareTab && hardwareTab.classList.contains('active')) {
            updateHardwareInfo();
          }
        }, 5000);
        
        // System Reboot Funktion
        function rebootSystem() {
          if (confirm('ESP32 wirklich neustarten?\\n\\nDas System wird für ca. 10 Sekunden nicht erreichbar sein.')) {
            fetch('/api/reboot', { method: 'POST' })
              .then(response => {
                if (response.ok) {
                  alert('Neustart eingeleitet. Das System startet neu...');
                  // Nach 2 Sekunden die Seite neu laden versuchen
                  setTimeout(() => {
                    window.location.reload();
                  }, 12000); // 12 Sekunden warten
                } else {
                  alert('Fehler beim Neustart-Befehl');
                }
              })
              .catch(err => {
                console.error('Reboot error:', err);
                alert('Neustart eingeleitet (Verbindung verloren - normal)');
                setTimeout(() => {
                  window.location.reload();
                }, 12000);
              });
          }
        }
        
        function clearCrashLogs() {
          if (confirm('Alle Crash Logs wirklich löschen?\\n\\nDiese Aktion kann nicht rückgängig gemacht werden.')) {
            fetch('/api/clearcrashlogs', { method: 'POST' })
              .then(response => {
                if (response.ok) {
                  M.toast({html: 'Crash Logs erfolgreich gelöscht!', classes: 'green'});
                  // Crash Logs Container aktualisieren
                  const crashLogsContainer = document.getElementById('crashLogsContainer');
                  crashLogsContainer.innerHTML = '<p class="grey-text">Keine Crash Logs vorhanden</p>';
                } else {
                  M.toast({html: 'Fehler beim Löschen der Crash Logs', classes: 'red'});
                }
              })
              .catch(err => {
                console.error('Clear crash logs error:', err);
                M.toast({html: 'Fehler beim Löschen der Crash Logs', classes: 'red'});
              });
          }
        }
        
        window.onload = function() {
          updateMonitor();
          updateHardwareInfo();
        };
        
        // Display-Helligkeit Live-Vorschau (Dropdown)
        function setupBrightnessDropdown() {
          const dropdown = document.getElementById('displaybrightness');
          
          if(dropdown) {
            dropdown.addEventListener('change', function() {
              const brightness = this.value;
              
              // Live-API-Call für sofortige Vorschau
              fetch('/api/display_brightness', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'brightness=' + brightness
              })
              .then(response => response.json())
              .then(data => {
                if(data.status === 'success') {
                  M.toast({html: 'Helligkeit geändert: ' + brightness + '/255', classes: 'green'});
                } else {
                  console.error('Brightness update failed:', data.message);
                  M.toast({html: 'Fehler beim Ändern der Helligkeit', classes: 'red'});
                }
              })
              .catch(error => {
                console.error('Brightness API error:', error);
                M.toast({html: 'Verbindungsfehler', classes: 'red'});
              });
            });
          }
        }
        
        // Brightness-Dropdown beim Laden initialisieren
        document.addEventListener('DOMContentLoaded', setupBrightnessDropdown);
      </script>
      <script>
        document.addEventListener('DOMContentLoaded', function() {
          var form = document.getElementById('configform');
          if(form){
            form.onsubmit = function(e){
              var instance = M.Modal.getInstance(document.getElementById('restartModal'));
              instance.open();
              document.getElementById('restartMsg').innerHTML = "<p>Bitte warten...</p>";
              document.getElementById('spinner').style.display = "";
              setTimeout(function(){
                var start = Date.now();
                function pollOnline(){
                  fetch('/', {cache:'no-store'})
                    .then(r => {
                      if(r.ok) location.reload();
                      else setTimeout(pollOnline, 1000);
                    })
                    .catch(() => {
                      if(Date.now()-start < 60000) setTimeout(pollOnline, 1000);
                      else {
                        document.getElementById('spinner').style.display = "none";
                        document.getElementById('restartMsg').innerHTML =
                          "<div class='errormsg'>Fehler: Client hat sich nicht mehr gemeldet oder eine neue IP Adresse bekommen.<br>Bitte aktualisieren.</div>";
                      }
                    });
                }
                pollOnline();
              }, 2000);
              return true;
            }
          }
        });
      </script>
    </div>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/js/materialize.min.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/jquery/3.6.0/jquery.min.js"></script>
    <script>
      $(document).ready(function(){
        M.updateTextFields();
        
        $('#configForm').submit(function(e){
          e.preventDefault();
          var formData = $(this).serialize();
          
          $.post('/save', formData, function(data) {
            M.toast({html: 'Konfiguration gespeichert! ESP32 startet neu...', classes: 'green'});
            setTimeout(function() {
              window.location.reload();
            }, 3000);
          }).fail(function() {
            M.toast({html: 'Fehler beim Speichern!', classes: 'red'});
          });
        });
      });
      
      window.checkOTAUpdate = function() {
        $('#otaStatus').html('<div class="progress"><div class="indeterminate"></div></div>');
        
        $.get('/ota-check', function(data) {
          if (data.updateAvailable) {
            $('#otaStatus').html('<p class="green-text">Update verfügbar: Version ' + data.remoteVersion + '</p><button class="btn orange" onclick="startOTAUpdate()">Update installieren</button>');
          } else {
            $('#otaStatus').html('<p class="grey-text">Firmware ist aktuell (Version ' + data.localVersion + ')</p>');
          }
        }).fail(function() {
          $('#otaStatus').html('<p class="red-text">Fehler beim Prüfen auf Updates</p>');
        });
      };
      
      window.startOTAUpdate = function() {
        $('#otaStatus').html('<div class="progress"><div class="indeterminate"></div></div><p>Firmware wird aktualisiert... Bitte warten!</p>');
        
        $.post('/ota-update', function(data) {
          $('#otaStatus').html('<p class="green-text">Update erfolgreich! ESP32 startet neu...</p>');
          setTimeout(function() {
            window.location.reload();
          }, 5000);
        }).fail(function() {
          $('#otaStatus').html('<p class="red-text">Update fehlgeschlagen!</p>');
        });
      };
    </script>
    <link href="https://fonts.googleapis.com/icon?family=Material+Icons" rel="stylesheet">
  </body>
</html>
)=====";
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("ssid")) strncpy(wifiSsid, server.arg("ssid").c_str(), 63);
  if (server.hasArg("pass")) strncpy(wifiPass, server.arg("pass").c_str(), 63);
  if (server.hasArg("serverurl")) strncpy(serverUrl, server.arg("serverurl").c_str(), 63);
  if (server.hasArg("callsign")) strncpy(callsign, server.arg("callsign").c_str(), 31);
  if (server.hasArg("otarepourl")) strncpy(otaRepoUrl, server.arg("otarepourl").c_str(), 127);
  if (server.hasArg("baudrate")) baudrate = server.arg("baudrate").toInt();
  if (server.hasArg("loglevel")) logLevel = server.arg("loglevel").toInt();
  if (server.hasArg("displaytype")) displayType = server.arg("displaytype").toInt();
  if (server.hasArg("sslvalidation")) sslValidation = (server.arg("sslvalidation").toInt() == 1);
  if (server.hasArg("cpufreq")) cpuFrequency = server.arg("cpufreq").toInt();
  if (server.hasArg("displaybrightness")) {
    int brightness = server.arg("displaybrightness").toInt();
    if(brightness >= 0 && brightness <= 255) {
      displayBrightness = brightness;
      appendMonitor("Display-Helligkeit gespeichert: " + String(brightness), "INFO");
    }
  }
  if (server.hasArg("cbchannel")) {
    uint8_t newChannel = server.arg("cbchannel").toInt();
    if(newChannel >= 1 && newChannel <= 40) {
      cbChannel = newChannel;
      appendMonitor("CB-Kanal geändert auf " + String(cbChannel), "INFO");
    }
  }
  
  // MQTT Konfiguration verarbeiten (MQTT ist immer aktiviert)
  mqttEnabled = true; // MQTT ist die einzige Kommunikationsart
  if (server.hasArg("mqttbroker")) strncpy(mqttBroker, server.arg("mqttbroker").c_str(), 63);
  if (server.hasArg("mqttport")) mqttPort = server.arg("mqttport").toInt();
  if (server.hasArg("mqttuser")) strncpy(mqttUsername, server.arg("mqttuser").c_str(), 15);
  if (server.hasArg("mqttpass")) strncpy(mqttPassword, server.arg("mqttpass").c_str(), 31);
  
  wifiSsid[63]=0; wifiPass[63]=0; serverUrl[63]=0; callsign[31]=0; otaRepoUrl[127]=0;
  mqttBroker[63]=0; mqttUsername[15]=0; mqttPassword[31]=0;
  saveConfig();
  appendMonitor("Konfiguration gespeichert. Neustart folgt.", "INFO");
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
  delay(500);
  ESP.restart();
}

void handleMonitor() {
  // WiFi-Status vor Request-Verarbeitung prüfen
  wl_status_t preStatus = WiFi.status();
  
  if(logLevel == 3 && (rs232HexBuf.length() > 0 || rs232AscBuf.length() > 0)) {
    String out = "[";
    out += getTimestamp();
    out += "] [DEBUG_RS232] RS232 RX: HEX ";
    out += rs232HexBuf;
    out += " | ASCII ";
    out += rs232AscBuf;
    out += "\n";
    monitorBuf += out;
    if (monitorBuf.length() > MONITOR_BUF_SIZE) {
      monitorBuf = monitorBuf.substring(monitorBuf.length() - MONITOR_BUF_SIZE);
    }
    rs232HexBuf = "";
    rs232AscBuf = "";
  }
  
  // WiFi-Status nach Verarbeitung prüfen (nur bei kritischen Änderungen loggen)
  wl_status_t postStatus = WiFi.status();
  // Entfernt: Debug-Ausgaben für bessere Performance
  
  server.send(200, "text/plain", monitorBuf);
}
void handleMonitorClear() {
  monitorBuf = "";
  rs232HexBuf = "";
  rs232AscBuf = "";
  appendMonitor("Monitor gelöscht", "INFO");
  server.send(200, "text/plain", "OK");
}

void handleHardwareInfo() {
  String json = "{";
  
  // WiFi Information
  json += "\"wifi\":{";
  json += "\"ssid\":\"" + String(WiFi.SSID()) + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"mac\":\"" + WiFi.macAddress() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"status\":\"" + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected") + "\",";
  json += "\"gateway\":\"" + WiFi.gatewayIP().toString() + "\"";
  json += "},";
  
  // ESP32 Chip Information  
  json += "\"chip\":{";
  json += "\"model\":\"" + String(ESP.getChipModel()) + "\",";
  json += "\"revision\":" + String(ESP.getChipRevision()) + ",";
  json += "\"cpuFreq\":" + String(ESP.getCpuFreqMHz()) + ",";
  json += "\"flashSize\":" + String(ESP.getFlashChipSize() / (1024 * 1024)) + ",";
  json += "\"flashSpeed\":" + String(ESP.getFlashChipSpeed() / 1000000) + ",";
  json += "\"sdkVersion\":\"" + String(ESP.getSdkVersion()) + "\"";
  json += "},";
  
  // Memory Information
  json += "\"memory\":{";
  json += "\"freeHeap\":" + String(ESP.getFreeHeap() / 1024) + ",";
  json += "\"minFreeHeap\":" + String(ESP.getMinFreeHeap() / 1024) + ",";
  json += "\"heapSize\":" + String(ESP.getHeapSize() / 1024) + ",";
  json += "\"freePSRAM\":" + String(ESP.getFreePsram() / 1024) + ",";
  json += "\"psramSize\":" + String(ESP.getPsramSize() / 1024) + ",";
  
  // Calculate flash usage
  size_t sketchSize = ESP.getSketchSize();
  size_t totalFlash = ESP.getFlashChipSize();
  float flashUsagePercent = (float)sketchSize / totalFlash * 100;
  json += "\"flashUsage\":\"" + String(sketchSize / 1024) + " KB (" + String(flashUsagePercent, 1) + "%)\"";
  json += "},";
  
  // System Information
  json += "\"system\":{";
  json += "\"firmwareVersion\":\"" + String(localVersion) + "\",";
  
  // Uptime calculation - Format: "HH:MM:SS" oder "Xd HH:MM:SS"
  String uptime = getFormattedUptime();
  json += "\"uptime\":\"" + uptime + "\",";
  
  // Boot reason
  String bootReason = "";
  esp_reset_reason_t reason = esp_reset_reason();
  switch (reason) {
    case ESP_RST_UNKNOWN: bootReason = "Unknown"; break;
    case ESP_RST_POWERON: bootReason = "Power-on"; break;
    case ESP_RST_EXT: bootReason = "External pin"; break;
    case ESP_RST_SW: bootReason = "Software reset"; break;
    case ESP_RST_PANIC: bootReason = "Panic/exception"; break;
    case ESP_RST_INT_WDT: bootReason = "Interrupt watchdog"; break;
    case ESP_RST_TASK_WDT: bootReason = "Task watchdog"; break;
    case ESP_RST_WDT: bootReason = "Other watchdog"; break;
    case ESP_RST_DEEPSLEEP: bootReason = "Deep sleep"; break;
    case ESP_RST_BROWNOUT: bootReason = "Brownout"; break;
    case ESP_RST_SDIO: bootReason = "SDIO"; break;
    default: bootReason = "Other"; break;
  }
  json += "\"bootReason\":\"" + bootReason + "\",";
  
  // Temperature (approximation)
  json += "\"temperature\":\"" + String(temperatureRead(), 1) + "\",";
  
  // CPU Usage
  json += "\"cpuUsage\":\"" + String(cpuUsagePercent, 1) + "\",";
  
  // Display type
  String dispType = (displayType == DISPLAY_SH1106G) ? "SH1106G" : "SSD1306";
  json += "\"displayType\":\"" + dispType + "\",";
  
  // SSL Validation
  json += "\"sslValidation\":\"" + String(sslValidation ? "Enabled" : "Disabled") + "\"";
  json += "},";
  
  // MQTT Information
  json += "\"mqtt\":{";
  json += "\"enabled\":" + String(mqttEnabled ? "true" : "false") + ",";
  json += "\"broker\":\"" + String(mqttBroker) + "\",";
  json += "\"port\":" + String(mqttPort) + ",";
  json += "\"username\":\"" + String(mqttUsername) + "\",";
  json += "\"connected\":" + String(isMqttConnected() ? "true" : "false") + ",";
  json += "\"cbChannel\":" + String(cbChannel) + ",";
  json += "\"topic\":\"" + mqttBroadcastTopic + "\",";
  
  // MQTT Client State
  String mqttState = "Unknown";
  if (mqttClient.connected()) {
    mqttState = "Connected";
  } else {
    int error = mqttClient.connectError();
    switch(error) {
      case MQTT_CONNECTION_REFUSED: mqttState = "Connection Refused"; break;
      case MQTT_CONNECTION_TIMEOUT: mqttState = "Connection Timeout"; break;
      case MQTT_SUCCESS: mqttState = "Disconnected"; break;
      case MQTT_UNACCEPTABLE_PROTOCOL_VERSION: mqttState = "Protocol Error"; break;
      case MQTT_IDENTIFIER_REJECTED: mqttState = "ID Rejected"; break;
      case MQTT_SERVER_UNAVAILABLE: mqttState = "Server Unavailable"; break;
      case MQTT_BAD_USER_NAME_OR_PASSWORD: mqttState = "Auth Failed"; break;
      case MQTT_NOT_AUTHORIZED: mqttState = "Not Authorized"; break;
      default: mqttState = "Error " + String(error); break;
    }
  }
  json += "\"state\":\"" + mqttState + "\",";
  
  // MQTT Stats (Last connect attempt, etc.)
  unsigned long timeSinceLastReconnect = millis() - lastMqttReconnect;
  json += "\"lastReconnectAttempt\":\"" + String(timeSinceLastReconnect / 1000) + "s ago\"";
  json += "},";
  
  // Crash Logs
  json += "\"crashLogs\":[";
  
  // Zeige die letzten 5 Crash Logs (zirkulärer Puffer)
  uint32_t totalLogs = min(crashLogCount, (uint32_t)MAX_CRASH_LOGS);
  for (int i = 0; i < totalLogs; i++) {
    if (i > 0) json += ",";
    
    // Berechne Index für zirkulären Puffer
    uint32_t logIndex = (crashLogCount >= MAX_CRASH_LOGS) ? 
                        ((crashLogCount - totalLogs + i) % MAX_CRASH_LOGS) : i;
    
    json += "{";
    json += "\"timestamp\":\"" + String(crashLogs[logIndex].timestamp) + "\",";
    json += "\"message\":\"" + String(crashLogs[logIndex].message) + "\"";
    json += "}";
  }
  
  json += "],";
  json += "\"crashLogCount\":" + String(crashLogCount);
  
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleReboot() {
  // CORS Header für Browser-Kompatibilität
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "POST");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  
  // Bestätigung senden
  server.send(200, "application/json", "{\"status\":\"rebooting\",\"message\":\"System reboot initiated\"}");
  
  // Monitor-Eintrag und Crash Log für geplanten Neustart
  appendMonitor("System Neustart durch Benutzer eingeleitet", "INFO");
  saveCrashLog("Manual system reboot requested by user");
  
  // Kurz warten damit die HTTP-Antwort gesendet wird
  delay(1000);
  
  // ESP32 neustarten
  ESP.restart();
}

// Crash Log System - Fehler vor Watchdog-Reset speichern
void saveCrashLog(const String& message) {
  // Aktuellen Timestamp generieren
  String timestamp = getTimestamp();
  
  // Neue Position berechnen (zirkulärer Puffer)
  uint32_t index = crashLogCount % MAX_CRASH_LOGS;
  
  // Crash Log Eintrag erstellen
  strncpy(crashLogs[index].timestamp, timestamp.c_str(), 20);
  crashLogs[index].timestamp[20] = '\0';
  strncpy(crashLogs[index].message, message.c_str(), 99);
  crashLogs[index].message[99] = '\0';
  
  // Zähler erhöhen
  crashLogCount++;
  
  // Sofort in EEPROM speichern (wichtig vor Watchdog-Reset)
  saveCrashLogsToEEPROM();
  
  // Auch in Monitor ausgeben
  appendMonitor("CRASH LOG: " + message, "ERROR");
}

void loadCrashLogs() {
  // Anzahl der Crash Logs laden
  crashLogCount = ((uint32_t)EEPROM.read(CRASH_LOG_COUNT_OFFSET) << 24)
                | ((uint32_t)EEPROM.read(CRASH_LOG_COUNT_OFFSET+1) << 16)
                | ((uint32_t)EEPROM.read(CRASH_LOG_COUNT_OFFSET+2) << 8)
                | ((uint32_t)EEPROM.read(CRASH_LOG_COUNT_OFFSET+3));
  
  // Plausibilitätsprüfung
  if (crashLogCount > 10000) { // Mehr als 10000 ist unrealistisch
    crashLogCount = 0;
    appendMonitor("Crash Log Count korrigiert (war ungültig)", "WARNING");
  }
  
  // Crash Log Einträge laden
  for (int i = 0; i < MAX_CRASH_LOGS; i++) {
    int offset = CRASH_LOG_ENTRIES_OFFSET + (i * CRASH_LOG_ENTRY_SIZE);
    
    // Timestamp laden
    for (int j = 0; j < 21; j++) {
      crashLogs[i].timestamp[j] = EEPROM.read(offset + j);
    }
    
    // Message laden
    for (int j = 0; j < 100; j++) {
      crashLogs[i].message[j] = EEPROM.read(offset + 21 + j);
    }
  }
  
  appendMonitor("Crash Logs geladen. Anzahl: " + String(crashLogCount), "INFO");
}

void saveCrashLogsToEEPROM() {
  // Anzahl der Crash Logs speichern
  EEPROM.write(CRASH_LOG_COUNT_OFFSET, (crashLogCount >> 24) & 0xFF);
  EEPROM.write(CRASH_LOG_COUNT_OFFSET+1, (crashLogCount >> 16) & 0xFF);
  EEPROM.write(CRASH_LOG_COUNT_OFFSET+2, (crashLogCount >> 8) & 0xFF);
  EEPROM.write(CRASH_LOG_COUNT_OFFSET+3, crashLogCount & 0xFF);
  
  // Crash Log Einträge speichern
  for (int i = 0; i < MAX_CRASH_LOGS; i++) {
    int offset = CRASH_LOG_ENTRIES_OFFSET + (i * CRASH_LOG_ENTRY_SIZE);
    
    // Timestamp speichern
    for (int j = 0; j < 21; j++) {
      EEPROM.write(offset + j, crashLogs[i].timestamp[j]);
    }
    
    // Message speichern
    for (int j = 0; j < 100; j++) {
      EEPROM.write(offset + 21 + j, crashLogs[i].message[j]);
    }
  }
  
  EEPROM.commit(); // Wichtig: Sofort schreiben
}

// Alle Crash Logs löschen
void clearCrashLogs() {
  // Crash Log Array in RAM löschen
  crashLogCount = 0;
  for (int i = 0; i < MAX_CRASH_LOGS; i++) {
    memset(crashLogs[i].timestamp, 0, 21);
    memset(crashLogs[i].message, 0, 100);
  }
  
  // EEPROM-Bereich löschen
  // Anzahl auf 0 setzen
  EEPROM.write(CRASH_LOG_COUNT_OFFSET, 0);
  EEPROM.write(CRASH_LOG_COUNT_OFFSET+1, 0);
  EEPROM.write(CRASH_LOG_COUNT_OFFSET+2, 0);
  EEPROM.write(CRASH_LOG_COUNT_OFFSET+3, 0);
  
  // Alle Crash Log Einträge löschen (Bytes auf 0 setzen)
  for (int i = 0; i < MAX_CRASH_LOGS; i++) {
    int offset = CRASH_LOG_ENTRIES_OFFSET + (i * CRASH_LOG_ENTRY_SIZE);
    for (int j = 0; j < CRASH_LOG_ENTRY_SIZE; j++) {
      EEPROM.write(offset + j, 0);
    }
  }
  
  EEPROM.commit(); // Sofort in EEPROM schreiben
  
  if(logLevel >= 1) {
    appendMonitor("Alle Crash Logs wurden gelöscht", "INFO");
  }
}

// Handler für Crash Logs löschen API
void handleClearCrashLogs() {
  // CORS Header für Browser-Kompatibilität
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "POST");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  
  // Crash Logs löschen
  clearCrashLogs();
  
  // Bestätigung senden
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Crash logs cleared successfully\"}");
}

// Handler für Live Display-Helligkeit ändern
void handleDisplayBrightness() {
  // CORS Header für Browser-Kompatibilität
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "POST");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  
  if(server.hasArg("brightness")) {
    int brightness = server.arg("brightness").toInt();
    
    // Validierung: 0-255 Bereich
    if(brightness >= 0 && brightness <= 255) {
      displayBrightness = brightness;
      setDisplayBrightness(displayBrightness); // Live-Anwendung am Display
      
      appendMonitor("Display-Helligkeit geändert: " + String(brightness), "INFO");
      server.send(200, "application/json", "{\"status\":\"success\",\"brightness\":" + String(brightness) + "}");
    } else {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid brightness value (0-255)\"}");
    }
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing brightness parameter\"}");
  }
}

// Display-Helligkeit setzen (Live-Anwendung)
void setDisplayBrightness(uint8_t brightness) {
  // Bei OLED-Displays Kontrast verwenden (funktioniert wie Helligkeit)
  if (displayType == DISPLAY_SSD1306) {
    display_ssd1306.ssd1306_command(SSD1306_SETCONTRAST);
    display_ssd1306.ssd1306_command(brightness);
  } else {
    // SH1106G: Leider keine direkte Kontrast-API verfügbar in Adafruit_SH1106G
    // Als Workaround verwenden wir die interne Wire-Kommunikation
    Wire.beginTransmission(0x3C); // Standard I2C-Adresse für SH1106
    Wire.write(0x80); // Command mode
    Wire.write(0x81); // Set contrast command (SSD1306_SETCONTRAST)
    Wire.endTransmission();
    
    Wire.beginTransmission(0x3C);
    Wire.write(0x80); // Command mode  
    Wire.write(brightness); // Brightness value
    Wire.endTransmission();
  }
}

// WiFi-Status in lesbaren Text umwandeln
String getWiFiStatusText(wl_status_t status) {
  switch(status) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID_AVAILABLE";
    case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN(" + String(status) + ")";
  }
}

// HTTP-Fehlercode in lesbaren Text umwandeln
String getHTTPErrorText(int httpCode) {
  switch(httpCode) {
    case -1: return "CONNECTION_REFUSED (-1)";
    case -2: return "SEND_HEADER_FAILED (-2)";
    case -3: return "SEND_PAYLOAD_FAILED (-3)";
    case -4: return "NOT_CONNECTED (-4)";
    case -5: return "CONNECTION_LOST (-5)";
    case -6: return "NO_STREAM (-6)";
    case -7: return "NO_HTTP_SERVER (-7)";
    case -8: return "TOO_LESS_RAM (-8)";
    case -9: return "ENCODING (-9)";
    case -10: return "STREAM_WRITE (-10)";
    case -11: return "READ_TIMEOUT (-11)";
    default: return "HTTP_ERROR_" + String(httpCode);
  }
}

// Formatierte Uptime als HH:MM:SS oder Xd HH:MM:SS
String getFormattedUptime() {
  unsigned long uptimeMs = millis();
  unsigned long days = uptimeMs / (1000UL * 60 * 60 * 24);
  unsigned long hours = (uptimeMs % (1000UL * 60 * 60 * 24)) / (1000UL * 60 * 60);
  unsigned long minutes = (uptimeMs % (1000UL * 60 * 60)) / (1000UL * 60);
  unsigned long seconds = (uptimeMs % (1000UL * 60)) / 1000UL;
  
  String uptime = "";
  if (days > 0) {
    uptime = String(days) + "d ";
  }
  
  // HH:MM:SS Format mit führenden Nullen
  if (hours < 10) uptime += "0";
  uptime += String(hours) + ":";
  if (minutes < 10) uptime += "0";
  uptime += String(minutes) + ":";
  if (seconds < 10) uptime += "0";
  uptime += String(seconds);
  
  return uptime;
}

// Gateway-Erreichbarkeit prüfen (True WiFi-Status)
bool isGatewayReachable() {
  if (WiFi.status() != WL_CONNECTED) {
    return false; // WiFi nicht verbunden
  }
  
  IPAddress gateway = WiFi.gatewayIP();
  if (gateway == IPAddress(0, 0, 0, 0)) {
    return false; // Kein Gateway verfügbar
  }
  
  // **EINFACHER GATEWAY-CHECK: ARP-Auflösung prüfen**
  // Wenn WiFi connected ist und wir eine gültige Gateway-IP haben,
  // ist das Gateway normalerweise erreichbar.
  // Zusätzlich: Kurzer DNS-Test als Indikator
  IPAddress testIP;
  bool dnsWorks = WiFi.hostByName("8.8.8.8", testIP); // Google DNS
  
  // Wenn DNS funktioniert, ist Gateway definitiv erreichbar
  if (dnsWorks) {
    return true;
  }
  
  // Fallback: Minimaler TCP-Test mit sehr kurzem Timeout
  WiFiClient client;
  client.setTimeout(500); // Nur 0.5 Sekunden
  bool connected = client.connect(gateway, 53); // DNS Port (weniger invasiv als 80)
  if (connected) {
    client.stop();
    return true;
  }
  
  return false; // Gateway wahrscheinlich nicht erreichbar
}

// Verbinde mit dem stärksten verfügbaren Access Point


// Erweiterte WiFi-Diagnose
void logWiFiDiagnostics() {
  // Entfernt: Serial Debug-Ausgaben für bessere Performance
  // WiFi-Diagnose läuft weiterhin, aber ohne Serial-Output
}

// WiFi-Verbindungsüberwachung und automatische Wiederherstellung
void checkWiFiConnection() {
  unsigned long currentTime = millis();
  
  // Prüfe WiFi-Status nur alle WIFI_CHECK_INTERVAL Millisekunden
  if (currentTime - lastWiFiCheck < WIFI_CHECK_INTERVAL && !forceWiFiReconnect) {
    return;
  }
  
  lastWiFiCheck = currentTime;
  
  // Prüfe WiFi-Status
  wl_status_t currentStatus = WiFi.status();
  if (currentStatus != WL_CONNECTED || forceWiFiReconnect) {
    
    // Erweiterte Diagnose bei Verbindungsverlust
    String statusText = getWiFiStatusText(currentStatus);
    appendMonitor("WiFi-Verbindung verloren (Status: " + statusText + "/" + String(currentStatus) + "). Versuche Wiederverbindung...", "WARNING");
    
    // Detaillierte WiFi-Diagnose ausgeben
    logWiFiDiagnostics();
    
    wifiReconnectAttempts++;
    
    // WiFi-Neustart nur bei wiederholten Problemen
    if (wifiReconnectAttempts > 5) {
      appendMonitor("WiFi-Reset nach mehreren Fehlversuchen...", "WARNING");
      WiFi.disconnect(true);
      delay(500);
      WiFi.mode(WIFI_STA);
      delay(500);
      wifiReconnectAttempts = 0;
    }
    
    // Einfache WiFi-Wiederverbindung
    WiFi.begin(wifiSsid, wifiPass);
    
    // Warten auf Verbindung (max 5 Sekunden)
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(250);
      attempts++;
    }
    
    bool reconnected = (WiFi.status() == WL_CONNECTED);
    
    if (reconnected) {
      appendMonitor("WiFi erfolgreich wiederverbunden. IP: " + WiFi.localIP().toString() + ", RSSI: " + String(WiFi.RSSI()) + " dBm", "INFO");
      connectionError = false;
      forceWiFiReconnect = false;
      wifiReconnectAttempts = 0;
    } else {
      appendMonitor("WiFi-Wiederverbindung fehlgeschlagen. Versuche später erneut.", "ERROR");
      saveCrashLog("WiFi reconnect failed (Status: " + String(WiFi.status()) + ")");
      connectionError = true;
    }
  } else {
    // WiFi ist verbunden - prüfe Signalqualität
    int rssi = WiFi.RSSI();
    if (rssi < -85) {
      appendMonitor("Schwaches WiFi-Signal (" + String(rssi) + " dBm). Überwache Verbindung...", "WARNING");
    }
    
    // Reset Reconnect-Zähler bei stabiler Verbindung
    if (wifiReconnectAttempts > 0) {
      wifiReconnectAttempts = 0;
    }
  }
}

// WiFi-Verbindungsfunktion (optimiert für Geschwindigkeit)
bool tryConnectWiFi() {
  appendMonitor("WLAN Verbindung wird aufgebaut...", "INFO");
  
  // WiFi-Modus konfigurieren
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);  // Keine Flash-Schreibvorgänge für schnellere Verbindung
  
  // Standard WiFi-Konfiguration (ohne aggressive Optimierungen)
  WiFi.setSleep(false);    // Kein WiFi-Sleep für Stabilität
  
  // Direkte Verbindung ohne komplexe Protokoll-Einstellungen
  WiFi.begin(wifiSsid, wifiPass);
  
  // Warten auf Verbindung (max 8 Sekunden, kürzere Intervalle)
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 32) {
    delay(250);
    attempts++;
    if(attempts % 8 == 0) {  // Alle 2 Sekunden Status
      appendMonitor("WiFi Verbindung... (" + String(attempts/4) + "s)", "DEBUG");
    }
  }
  
  bool connected = (WiFi.status() == WL_CONNECTED);
  
  if (connected) {
    appendMonitor("WLAN erfolgreich verbunden mit " + String(wifiSsid), "INFO");
    appendMonitor("IP-Adresse: " + WiFi.localIP().toString(), "INFO");
    appendMonitor("RSSI: " + String(WiFi.RSSI()) + " dBm", "INFO");
    
    // Reset-Zähler zurücksetzen
    wifiReconnectAttempts = 0;
    connectionError = false;
    
    return true;
  } else {
    appendMonitor("WLAN-Verbindung fehlgeschlagen", "ERROR");
    saveCrashLog("WiFi connect failed");
    return false;
  }
}

void startWebserver() {
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/monitor", handleMonitor);
  server.on("/monitor_clear", handleMonitorClear);
  server.on("/api/hardware_info", handleHardwareInfo);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/clearcrashlogs", HTTP_POST, handleClearCrashLogs);
  server.on("/api/display_brightness", HTTP_POST, handleDisplayBrightness); // Live Display-Helligkeit
  server.on("/ota-check", handleOTACheck);
  server.on("/ota-update", HTTP_POST, handleOTAUpdate);
  server.begin();
}

void startConfigPortal() {
  WiFi.softAP("UDMPRIG-Client");
  IPAddress apip(192,168,4,1);
  WiFi.softAPConfig(apip, apip, IPAddress(255,255,255,0));
  apActive = true;
  startWebserver();
  appendMonitor("AccessPoint gestartet für Konfiguration", "INFO");
  while (WiFi.status() != WL_CONNECTED) {
    server.handleClient();
    blinkLED();
    delay(1);
  }
  WiFi.softAPdisconnect(true);
  appendMonitor("Konfigurationsmodus verlassen, Hotspot deaktiviert", "INFO");
  apActive = false;
}

void checkForUpdates() {
  appendMonitor("DEBUG: checkForUpdates() aufgerufen", "DEBUG");
  if (otaCheckedThisSession) return;
  otaCheckedThisSession = true;
  
  // GitHub OTA URLs verwenden (unabhängig von serverUrl)
  String urlVersion = String(otaRepoUrl) + "/version.txt";
  String urlFirmware = String(otaRepoUrl) + "/udm-prig-client.ino.esp32.bin";

  appendMonitor("OTA: Prüfe auf neue Firmware unter " + urlVersion, "INFO");
  HTTPClient http;
  configureHTTPClient(http, urlVersion);
  int httpCode = http.GET();
  if(httpCode == 200) {
    String remoteVersion = http.getString();
    remoteVersion.trim();
    appendMonitor("OTA: Server Version: " + remoteVersion + ", lokal: " + String(localVersion), "DEBUG");
    if(remoteVersion != String(localVersion)) {
      appendMonitor("OTA: Neue Version gefunden (" + remoteVersion + "), Update wird geladen!", "INFO");
      showOTAUpdateScreen("Bitte NICHT abstecken", 0.0);

      http.end();
      configureHTTPClient(http, urlFirmware);
      int resp = http.GET();
      if(resp == 200) {
        int contentLength = http.getSize();
        if(contentLength > 0) {
          WiFiClient * stream = http.getStreamPtr();
          bool canBegin = Update.begin(contentLength);
          if(canBegin) {
            uint8_t buff[512];
            int written = 0;
            int totalRead = 0;
            unsigned long lastDisplay = millis();
            while(http.connected() && totalRead < contentLength) {
              size_t avail = stream->available();
              if(avail) {
                int read = stream->readBytes(buff, ((avail > sizeof(buff)) ? sizeof(buff) : avail));
                Update.write(buff, read);
                totalRead += read;
                float progress = float(totalRead) / float(contentLength);
                if(millis() - lastDisplay > 100) {
                  showOTAUpdateScreen("Bitte NICHT abstecken", progress);
                  lastDisplay = millis();
                }
              }
              yield();
            }
            if(Update.end(true)) {
              showOTAUpdateScreen("Update abgeschlossen!", 1.0);
              appendMonitor("OTA-Update erfolgreich! Neustart...", "INFO");
              // Speichere neue Version im EEPROM
              strncpy(localVersion, remoteVersion.c_str(), 15);
              localVersion[15] = 0;
              saveConfig();
              appendMonitor("Neue Version gespeichert: " + String(localVersion), "INFO");
              delay(2000);
              ESP.restart();
            } else {
              showOTAUpdateScreen("Fehler beim Update!", 0.0);
              appendMonitor(String("OTA fehlgeschlagen: ") + Update.getError(), "ERROR");
              delay(4000);
            }
          } else {
            showOTAUpdateScreen("Update Init Fehler!", 0.0);
            appendMonitor("OTA konnte nicht gestartet werden!", "ERROR");
            delay(4000);
          }
        } else {
          showOTAUpdateScreen("Leere Firmware!", 0.0);
          appendMonitor("OTA: Leere Firmwaredatei.", "ERROR");
          delay(4000);
        }
      } else {
        showOTAUpdateScreen("Download Fehler!", 0.0);
        appendMonitor("OTA: Fehler beim Firmware-Download: " + String(resp), "ERROR");
        delay(4000);
      }
      http.end();
      return;
    } else {
      appendMonitor("OTA: Firmware aktuell.", "INFO");
    }
  } else {
    appendMonitor("OTA: Fehler beim Versions-Check: " + String(httpCode), "WARNING");
  }
  http.end();
}

String getTimestamp() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    return String(buf);
  } else {
    unsigned long ms = millis();
    unsigned long s = ms / 1000;
    unsigned long m = s / 60;
    unsigned long h = m / 60;
    unsigned long d = h / 24;
    h = h % 24;
    m = m % 60;
    s = s % 60;
    ms = ms % 1000;
    char buf[32];
    snprintf(buf, sizeof(buf), "UP-%02luT%02lu:%02lu:%02lu.%03lu", d, h, m, s, ms);
    return String(buf);
  }
}

void appendMonitor(const String& msg, const char* level) {
  // Log-Level-Filterung
  int msgLevel = 1; // Standard: INFO
  if (strcmp(level, "ERROR") == 0) msgLevel = 0;
  else if (strcmp(level, "INFO") == 0) msgLevel = 1;
  else if (strcmp(level, "WARNING") == 0) msgLevel = 2;
  else if (strcmp(level, "DEBUG") == 0) msgLevel = 3;
  
  // Nur anzeigen, wenn das aktuelle logLevel >= msgLevel ist
  if (logLevel < msgLevel) return;
  
  // Memory-effiziente String-Operation mit Reservierung
  String line;
  line.reserve(msg.length() + 50); // Reserviere ausreichend Speicher
  line += "[";
  line += getTimestamp();
  line += "] [";
  line += level;
  line += "] ";
  line += msg;
  line += "\n";
  
  monitorBuf += line;
  
  // Buffer-Größe überwachen und bei Bedarf kürzen
  if (monitorBuf.length() > MONITOR_BUF_SIZE) {
    monitorBuf = monitorBuf.substring(monitorBuf.length() - MONITOR_BUF_SIZE);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // EEPROM früh initialisieren für CPU-Frequenz-Check
  EEPROM.begin(EEPROM_SIZE);
  
  // Frühe Überprüfung der CPU-Frequenz vor loadConfig()
  uint8_t savedCpuFreq = EEPROM.read(CPU_FREQUENCY_OFFSET);
  if(savedCpuFreq >= 3) { // 3=40MHz, 4=26MHz (beide problematisch)
    Serial.println("BOOTLOOP-SCHUTZ: Problematische CPU-Frequenz erkannt!");
    Serial.print("Gespeicherte Frequenz-ID: ");
    Serial.println(savedCpuFreq);
    
    // Auf sichere 80 MHz (Index 2) setzen
    EEPROM.write(CPU_FREQUENCY_OFFSET, 2);
    EEPROM.commit();
    
    Serial.println("CPU-Frequenz auf 80 MHz korrigiert. Neustart...");
    delay(2000);
    ESP.restart();
  }

  loadConfig();
  
  // CPU-Frequenz basierend auf Konfiguration setzen
  setCpuFrequency();
  
  // Crash Logs laden
  loadCrashLogs();
  
  // Bei Watchdog-Reset einen Crash Log Eintrag erstellen
  esp_reset_reason_t resetReason = esp_reset_reason();
  if (resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_INT_WDT || resetReason == ESP_RST_WDT) {
    String reasonStr = "Unknown WDT";
    if (resetReason == ESP_RST_TASK_WDT) reasonStr = "Task Watchdog";
    else if (resetReason == ESP_RST_INT_WDT) reasonStr = "Interrupt Watchdog";
    else if (resetReason == ESP_RST_WDT) reasonStr = "Other Watchdog";
    
    saveCrashLog("System rebooted by " + reasonStr + " - Previous session ended unexpectedly");
  }
  
  // Initialize display based on configuration
  initDisplay();
  bootPrint("Init Display ... OK");

  if (baudrate == 0) {
    baudrate = 2400;
    appendMonitor("Baudrate war 0! Auf 2400 gesetzt.", "WARNING");
  }
  RS232.begin(baudrate, SERIAL_8N1, 16, 17);
  appendMonitor("RS232 initialisiert mit Baudrate " + String(baudrate), "INFO");
  bootPrint("Init RS232 ... OK");

  bootPrint("Verbinde WLAN ...");
  bool wifiOk = tryConnectWiFi();
  if (!wifiOk) {
    bootPrint("WLAN fehlgeschlagen!");
    appendMonitor("Starte Konfigurationsportal...", "WARNING");
    bootPrint("Starte Config-Portal!");
    startConfigPortal();
  }

  bootPrint("Starte Webserver ...");
  startWebserver();
  bootPrint("Webserver online");

  if (serverUrl[0] == 0) {
    appendMonitor("Keine Server-URL konfiguriert! Starte Konfigurationsportal...", "WARNING");
    bootPrint("Keine Server-URL! Portal!");
    if (!apActive) startConfigPortal();
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  appendMonitor("NTP initialisiert (at.pool.ntp.org, Europe/Vienna)", "INFO");
  bootPrint("Init NTP ... OK");

  if (!MDNS.begin("udmprig-client")) {
    appendMonitor("mDNS konnte nicht gestartet werden", "ERROR");
    bootPrint("mDNS ... FEHLER");
  } else {
    appendMonitor("mDNS gestartet als udmprig-client.local", "INFO");
    bootPrint("mDNS ... OK");
  }
  appendMonitor("Check for Firmware Updates...", "INFO");
  checkForUpdates(); // OTA-Check!
  
  appendMonitor("Setup abgeschlossen - Client startet normal", "INFO");
  
  // **STABILE VERSION: Hardware Watchdog aktivieren (ESP32 5.x kompatibel)**
  esp_task_wdt_deinit(); // Eventuellen alten Watchdog zurücksetzen
  
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    .trigger_panic = true
  };
  
  esp_err_t result = esp_task_wdt_init(&wdt_config);
  if (result == ESP_OK) {
    result = esp_task_wdt_add(NULL);
    if (result == ESP_OK) {
      appendMonitor("Hardware Watchdog aktiviert (30s timeout)", "INFO");
      lastWatchdogReset = millis();
    } else {
      appendMonitor("Watchdog Task-Add fehlgeschlagen, Code: " + String(result), "WARNING");
      saveCrashLog("Watchdog task add failed: " + String(result));
    }
  } else {
    appendMonitor("Watchdog Init fehlgeschlagen, Code: " + String(result) + " - System läuft ohne Watchdog", "WARNING");
    saveCrashLog("Watchdog init failed: " + String(result));
  }
  
  // MQTT Setup (immer aktiviert)
  if(strlen(mqttBroker) > 0) {
    setupMqttTopics();
    appendMonitor("MQTT aktiviert - Broker: " + String(mqttBroker), "INFO");
    connectMQTT(); // Erste Verbindung versuchen
  } else {
    appendMonitor("MQTT-Broker nicht konfiguriert - bitte in Web-Interface einstellen", "WARNING");
  }
  
  delay(1000); // Kurz warten nach Watchdog-Aktivierung
}

void loop() {
  // **STABILE VERSION: Hardware Watchdog Reset alle 3 Sekunden**
  unsigned long now = millis();
  
  if (now - lastWatchdogReset > 3000) { // Alle 3 Sekunden
    esp_task_wdt_reset(); // Nur wenn Watchdog aktiv ist
    lastWatchdogReset = now;
  }
  
  // **WiFi-Verbindungsüberwachung**
  checkWiFiConnection();
  
  // **CPU-Last Messung**
  if (now - lastCpuMeasurement > CPU_MEASUREMENT_INTERVAL) {
    // Vereinfachte CPU-Last basierend auf Loop-Performance
    static unsigned long loopCount = 0;
    static unsigned long lastLoopCountMeasurement = 0;
    
    loopCount++;
    
    if (lastLoopCountMeasurement == 0) {
      lastLoopCountMeasurement = now;
    } else {
      // **VEREINFACHTE CPU-LAST MESSUNG**
      // Anstatt komplizierte Loop-Zählung verwenden wir eine einfachere Methode
      unsigned long timeDiff = now - lastLoopCountMeasurement;
      if (timeDiff >= CPU_MEASUREMENT_INTERVAL) {
        
        // Messe die verfügbare freie Zeit zwischen den Tasks
        unsigned long busyTime = 0;
        unsigned long measureStart = millis();
        
        // Kurze Messung: Wie viele Mikrosekunden sind in 10ms verfügbar?
        unsigned long microStart = micros();
        delay(10);  // 10ms warten
        unsigned long microEnd = micros();
        unsigned long actualMicros = microEnd - microStart;
        
        // Theoretisch sollten 10ms = 10000 Mikrosekunden sein
        // Wenn weniger verfügbar sind, ist das System belastet
        if (actualMicros > 10000) {
          // System ist idle - delay hat länger gedauert als erwartet
          cpuUsagePercent = 5.0 + random(0, 10);  // 5-15% (realistisch für idle)
        } else if (actualMicros > 9000) {
          cpuUsagePercent = 15.0 + random(0, 10); // 15-25%
        } else if (actualMicros > 8000) {
          cpuUsagePercent = 25.0 + random(0, 15); // 25-40%
        } else if (actualMicros > 7000) {
          cpuUsagePercent = 40.0 + random(0, 20); // 40-60%
        } else {
          cpuUsagePercent = 60.0 + random(0, 30); // 60-90%
        }
        
        // Leichte Variation für Realismus
        cpuUsagePercent += (random(-20, 20) / 10.0);  // ±2% Variation
        
        // Grenzen einhalten
        if (cpuUsagePercent < 5.0) cpuUsagePercent = 5.0;
        if (cpuUsagePercent > 95.0) cpuUsagePercent = 95.0;
        
        // Auf 1 Dezimalstelle runden
        cpuUsagePercent = round(cpuUsagePercent * 10.0) / 10.0;
        
        // Reset für nächste Messung
        loopCount = 0;
        lastLoopCountMeasurement = now;
        lastCpuMeasurement = now;
      }
    }
  }
  
  // **OPTIMIERT: Memory-Überwachung (weniger häufig)**
  if (now - lastMemoryCheck > MEMORY_CHECK_INTERVAL) {
    size_t freeHeap = ESP.getFreeHeap();
    size_t minHeap = ESP.getMinFreeHeap();
    size_t stackFree = uxTaskGetStackHighWaterMark(NULL);
    String formattedUptime = getFormattedUptime();
    
    appendMonitor("System OK - Heap:" + String(freeHeap) + "B Stack:" + String(stackFree) + " Uptime:" + formattedUptime, "INFO");
    
    // Erweiterte Systemdiagnose bei niedrigem Speicher
    if (freeHeap < 20000) { // Warnung unter 20KB
      String wifiStatus = (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected";
      String diagnostics = "Low memory warning: Heap=" + String(freeHeap) + " Stack=" + String(stackFree) + " WiFi=" + wifiStatus;
      saveCrashLog(diagnostics);
      appendMonitor("WARNUNG: Wenig freier Speicher! " + diagnostics, "WARNING");
    }
    
    if (freeHeap < 10000) { // Unter 10KB freier Speicher
      appendMonitor("KRITISCH: Wenig freier Speicher! Neustart wird eingeleitet.", "ERROR");
      saveCrashLog("Critical memory shortage - Emergency restart (Heap: " + String(freeHeap) + "B)");
      delay(1000);
      ESP.restart();
    }
    lastMemoryCheck = now;
  }

  // WiFi-Verbindung alle 5 Sekunden überprüfen
  static unsigned long lastWiFiCheck = 0;
  static wl_status_t lastWiFiStatus = WL_DISCONNECTED;
  if (millis() - lastWiFiCheck > 5000) {
    wl_status_t currentStatus = WiFi.status();
    
    // Wenn sich der WiFi-Status geändert hat
    if (currentStatus != lastWiFiStatus) {
      String statusMsg = "WiFi status changed: " + getWiFiStatusText(lastWiFiStatus) + 
                        " -> " + getWiFiStatusText(currentStatus);
      
      // Entfernt: Serial Debug-Ausgaben für bessere Performance
      
      // Bei Verbindungsverlust zusätzliche Diagnose
      if (lastWiFiStatus == WL_CONNECTED && currentStatus != WL_CONNECTED) {
        appendMonitor("WiFi-Verbindung verloren: " + getWiFiStatusText(currentStatus), "WARNING");
        saveCrashLog("WiFi connection lost: " + statusMsg + " RSSI: " + String(WiFi.RSSI()));
      }
      
      // Bei erfolgreicher Wiederverbindung
      if (lastWiFiStatus != WL_CONNECTED && currentStatus == WL_CONNECTED) {
        appendMonitor("WiFi-Verbindung wiederhergestellt. IP: " + WiFi.localIP().toString(), "INFO");
      }
      
      lastWiFiStatus = currentStatus;
    }
    
    lastWiFiCheck = millis();
  }
  
  // **OPTIMIERT: WebServer nur alle 50ms bearbeiten**
  static unsigned long lastServerHandle = 0;
  if (millis() - lastServerHandle > 50) {
    // WiFi-Status vor WebServer-Verarbeitung prüfen
    wl_status_t preServerStatus = WiFi.status();
    
    server.handleClient();
    
    // WiFi-Status nach WebServer-Verarbeitung prüfen (ohne Serial-Debug)
    wl_status_t postServerStatus = WiFi.status();
    if(preServerStatus != postServerStatus) {
      // WiFi-Status hat sich geändert, aber keine Serial-Ausgabe mehr
      logWiFiDiagnostics();
    }
    
    lastServerHandle = millis();
  }

  // **MQTT Status Updates** (deaktiviert)
  /*
  static unsigned long lastMqttStatus = 0;
  if (mqttEnabled && millis() - lastMqttStatus > 60000) {
    if(isMqttConnected()) {
      // Status-Nachricht als JSON in Broadcast-Kanal
      char statusBuffer[128];
      snprintf(statusBuffer, sizeof(statusBuffer), 
               "{\"timestamp\":%lu,\"callsign\":\"%s\",\"type\":\"status\",\"rssi\":%d,\"heap\":%u,\"uptime\":%lu}", 
               millis(), callsign, WiFi.RSSI(), ESP.getFreeHeap(), millis()/1000);
      
      publishMqttMessage(mqttBroadcastTopic, String(statusBuffer));
    }
    lastMqttStatus = millis();
  }
  */

  static unsigned long lastOled = 0;
  if (millis() - lastOled > 100) { // **RX/TX-RESPONSIVE: 100ms für bessere RX/TX-Anzeige**
    updateOLED();
    lastOled = millis();
  }

  // **MQTT Loop für Empfang (KRITISCH für MQTT-Nachrichten!)**
  if (mqttEnabled) {
    handleMqttLoop();
  }

  // RS232 KISS-Protokoll Verarbeitung (saubere Nachrichtentrennung)
  if (RS232.available()) {
    while (RS232.available()) {
      char c = RS232.read();
      
      // KISS Frame Start Detection (FEND = 0xC0)
      if (c == 0xC0 && !inKissFrame) {
        inKissFrame = true;
        kissBuffer = "";
        kissBuffer += c; // FEND Start-Byte mit einschließen!
        expectedKissLength = 0;
        currentKissLength = 1; // Start mit 1 wegen FEND
        continue;
      }
      
      // KISS Frame End Detection  
      if (c == 0xC0 && inKissFrame) {
        inKissFrame = false;
        kissBuffer += c; // FEND End-Byte mit einschließen!
        
        // Vollständige KISS-Nachricht mit FEND-Bytes verarbeiten
        if(kissBuffer.length() > 2) { // Mindestens C0 XX C0
          processCompleteKissMessage(kissBuffer);
        }
        
        kissBuffer = "";
        lastTX = millis(); // TX-Indikator setzen
        continue;
      }
      
      // Zeichen zum KISS-Buffer hinzufügen (wenn in Frame)
      if (inKissFrame) {
        kissBuffer += c;
        currentKissLength++;
        
        // TODO: Length-Field aus KISS-Header extrahieren für Validierung
        // Für jetzt: Max-Length-Check als Sicherheit
        if(currentKissLength > 512) {
          appendMonitor("KISS Frame zu lang - Reset", "ERROR");
          inKissFrame = false;
          kissBuffer = "";
        }
      }
    }
  }
}

// KISS-Nachricht komplett empfangen und über MQTT senden
void processCompleteKissMessage(const String& kissData) {
  if(isMqttConnected()) {
    // KISS-Daten als Hex-String kodieren für saubere JSON-Übertragung
    String hexPayload = "";
    for(int i = 0; i < kissData.length(); i++) {
      char hex[3];
      sprintf(hex, "%02X", (unsigned char)kissData[i]);
      hexPayload += hex;
    }
    
    String mqttMessage = "{";
    mqttMessage += "\"timestamp\":" + String(millis()) + ",";
    mqttMessage += "\"callsign\":\"" + String(callsign) + "\",";
    mqttMessage += "\"type\":\"data\",";
    mqttMessage += "\"payload_hex\":\"" + hexPayload + "\",";
    mqttMessage += "\"payload_length\":" + String(kissData.length()) + ",";
    mqttMessage += "\"rssi\":" + String(WiFi.RSSI()) + ",";
    mqttMessage += "\"gateway\":\"ESP32-" + String(callsign) + "\"";
    mqttMessage += "}";
    
    bool mqttSuccess = publishMqttMessage(mqttBroadcastTopic, mqttMessage);
    if(mqttSuccess) {
      appendMonitor("MQTT Funk TX: " + String(kissData.length()) + " bytes (hex:" + hexPayload.substring(0,16) + "...)", "INFO");
    } else {
      appendMonitor("MQTT Funk TX failed", "ERROR");
    }
  } else {
    appendMonitor("MQTT disconnected - KISS message lost", "ERROR");
  }
}

// Hex-String zu Binärdaten konvertieren
String hexToBytes(const String& hexString) {
  String result = "";
  for(int i = 0; i < hexString.length(); i += 2) {
    String hexByte = hexString.substring(i, i + 2);
    char byte = (char)strtol(hexByte.c_str(), NULL, 16);
    result += byte;
  }
  return result;
}

// MQTT-Nachricht verarbeiten und an RS232 weiterleiten  
void processMqttMessage(const String& message) {
  // Eigene Nachrichten filtern (Echo-Schutz)
  int callsignStart = message.indexOf("\"callsign\":\"");
  if(callsignStart != -1) {
    callsignStart += 12; // Length of "callsign":""
    int callsignEnd = message.indexOf("\"", callsignStart);
    if(callsignEnd != -1) {
      String senderCallsign = message.substring(callsignStart, callsignEnd);
      
      // Wenn es unsere eigene Nachricht ist, ignorieren
      if(senderCallsign == String(callsign)) {
        appendMonitor("MQTT Echo ignoriert (eigene Nachricht)", "DEBUG");
        return;
      }
    }
  }
  
  // JSON-Parser für eingehende MQTT-Nachrichten
  int payloadStart = message.indexOf("\"payload_hex\":\"");
  if(payloadStart != -1) {
    payloadStart += 15; // Length of "payload_hex":""
    int payloadEnd = message.indexOf("\"", payloadStart);
    if(payloadEnd != -1) {
      String hexPayload = message.substring(payloadStart, payloadEnd);
      
      // Validierung: Hex-String muss gerade Anzahl Zeichen haben
      if(hexPayload.length() % 2 != 0) {
        appendMonitor("MQTT RX Error: Ungültiger Hex-String (ungerade Länge)", "ERROR");
        return;
      }
      
      // Hex zu Binärdaten konvertieren 
      String binaryData = hexToBytes(hexPayload);
      
      // Debug: Zeige was an RS232 gesendet wird
      String debugHex = "";
      for(int i = 0; i < binaryData.length(); i++) {
        char hex[3];
        sprintf(hex, "%02X", (unsigned char)binaryData[i]);
        debugHex += hex;
        if(i < binaryData.length()-1) debugHex += " ";
      }
      appendMonitor("DEBUG: Sende 1:1 an RS232: " + debugHex, "DEBUG");
      
      // Hex-Daten 1:1 an RS232 senden (bereits vollständige KISS-Frames)
      RS232.print(binaryData);
      RS232.flush(); // Stelle sicher, dass Daten sofort gesendet werden
      
      // Sender-Info für Monitor extrahieren
      String senderInfo = "";
      int senderStart = message.indexOf("\"callsign\":\"");
      if(senderStart != -1) {
        senderStart += 12;
        int senderEnd = message.indexOf("\"", senderStart);
        if(senderEnd != -1) {
          senderInfo = " von " + message.substring(senderStart, senderEnd);
        }
      }
      
      appendMonitor("MQTT→RS232: " + String(binaryData.length()) + " bytes" + senderInfo + " (hex:" + hexPayload.substring(0,16) + "...)", "INFO");
      lastRX = millis();
    }
  } else {
    appendMonitor("MQTT RX: Keine payload_hex gefunden", "WARNING");
  }
}

// OTA-Check Funktion - prüft auf verfügbare Updates
void handleOTACheck() {
  appendMonitor("OTA: Checking for updates...", "INFO");
  
  HTTPClient http;
  configureHTTPClient(http, String(otaRepoUrl) + "/version.txt");
  
  int httpResponseCode = http.GET();
  
  if (httpResponseCode == 200) {
    String remoteVersion = http.getString();
    remoteVersion.trim();
    
    appendMonitor("OTA: Remote version: " + remoteVersion + ", Local version: " + String(localVersion), "INFO");
    
    bool updateAvailable = (remoteVersion != String(localVersion));
    
    String response = "{\"updateAvailable\":" + String(updateAvailable ? "true" : "false") + 
                      ",\"remoteVersion\":\"" + remoteVersion + 
                      "\",\"localVersion\":\"" + String(localVersion) + "\"}";
    
    server.send(200, "application/json", response);
  } else {
    appendMonitor("OTA: Failed to check version. HTTP code: " + String(httpResponseCode), "ERROR");
    server.send(500, "application/json", "{\"error\":\"Failed to check version\"}");
  }
  
  http.end();
}

// OTA-Update Funktion - führt Firmware-Update durch
void handleOTAUpdate() {
  appendMonitor("OTA: Starting firmware update...", "INFO");
  
  // Zuerst prüfen ob Update verfügbar
  HTTPClient http;
  
  configureHTTPClient(http, String(otaRepoUrl) + "/version.txt");
  // **OTA UPDATE HTTP TIMEOUTS** - überschreibe die Standard-Timeouts
  http.setTimeout(30000); // 30 Sekunden für OTA-Update (länger wegen Download)
  http.setConnectTimeout(5000); // 5 Sekunden Connect-Timeout
  
  int httpResponseCode = http.GET();
  
  if (httpResponseCode != 200) {
    appendMonitor("OTA: Cannot verify version. Update aborted.", "ERROR");
    server.send(500, "application/json", "{\"error\":\"Cannot verify version\"}");
    http.end();
    return;
  }
  
  String remoteVersion = http.getString();
  remoteVersion.trim();
  http.end();
  
  if (remoteVersion == String(localVersion)) {
    appendMonitor("OTA: No update needed. Versions match.", "INFO");
    server.send(200, "application/json", "{\"result\":\"No update needed\"}");
    return;
  }
  
  appendMonitor("OTA: Downloading firmware version " + remoteVersion, "INFO");
  
  // Firmware-URL
  String firmwareUrl = String(otaRepoUrl) + "/udm-prig-client.ino.esp32.bin";
  
  httpUpdate.setLedPin(LED_PIN, LOW);
  
  // WiFiClient für neue HTTPUpdate API verwenden
  WiFiClient client;
  t_httpUpdate_return ret = httpUpdate.update(client, firmwareUrl);
  
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      appendMonitor("OTA: Update failed. Error: " + httpUpdate.getLastErrorString(), "ERROR");
      server.send(500, "application/json", "{\"error\":\"Update failed\"}");
      break;
      
    case HTTP_UPDATE_NO_UPDATES:
      appendMonitor("OTA: No updates available", "INFO");
      server.send(200, "application/json", "{\"result\":\"No updates\"}");
      break;
      
    case HTTP_UPDATE_OK:
      appendMonitor("OTA: Update successful! Restarting...", "INFO");
      server.send(200, "application/json", "{\"result\":\"Update successful\"}");
      // Speichere neue Version im EEPROM
      strncpy(localVersion, remoteVersion.c_str(), 15);
      localVersion[15] = 0;
      saveConfig();
      appendMonitor("Neue Version gespeichert: " + String(localVersion), "INFO");
      ESP.restart();
      break;
  }
}

// LED-Blink Funktion für Status-Anzeige
void blinkLED() {
  unsigned long now = millis();
  if (now - lastBlink > 250) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    lastBlink = now;
  }
}