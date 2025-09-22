#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <ESPmDNS.h>
#include <time.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <HTTPUpdate.h>
#include <esp_task_wdt.h>

// Watchdog-Timer Konfiguration
#define WDT_TIMEOUT 30  // 30 Sekunden Watchdog-Timeout
#define EEPROM_SIZE 347  // Reduziert: Display-Typ + SSL-Validierung (HTTPS auto-detect)
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
const unsigned long RS232_ACTIVE_TIME = 200;

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
bool apActive = false;
bool authenticationError = false;
bool sslCertificateError = false; // Neuer Flag für SSL-Zertifikatsfehler
bool connectionError = false; // Flag für allgemeine Verbindungsfehler (Server offline, etc.)

// Smart-Polling-Variablen
unsigned long smartPollInterval = 2000; // Dynamisches Intervall (Standard: 2s, max: 2s, min: 0.5s)

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

void appendMonitor(const String& msg, const char* level = "INFO");
String getTimestamp();
void blinkLED();
String decodeBase64Simple(String input); // Forward-Deklaration für Smart-Polling
String decodeKissFrame(String rawData); // Forward-Deklaration für KISS-Dekodierung
void handleOTACheck(); // Forward-Deklaration für OTA
void handleOTAUpdate(); // Forward-Deklaration für OTA
void handleHardwareInfo(); // Forward-Deklaration für Hardware Info API
bool initDisplay(); // Forward-Deklaration für Display-Initialisierung
void configureHTTPClient(HTTPClient &http, String url); // Forward-Deklaration für HTTPS-Konfiguration

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
  } else {
    if (!display_sh1106.begin(0x3C)) {
      return false;
    }
    display_sh1106.clearDisplay();
    display_sh1106.display();
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
  
  // Standard-Timeouts
  http.setTimeout(5000);
  http.setConnectTimeout(3000);
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

void updateOLED() {
  if (displayType == DISPLAY_SSD1306) {
    display_ssd1306.clearDisplay();
    display_ssd1306.setTextColor(getDisplayWhite());
    display_ssd1306.setTextSize(2);
    display_ssd1306.setCursor(0,1);
    display_ssd1306.print(callsign);
    int rssi = WiFi.RSSI();
    int strength = 0;
    if (WiFi.status() == WL_CONNECTED) {
      if (rssi > -55) strength = 4;
      else if (rssi > -65) strength = 3;
      else if (rssi > -75) strength = 2;
      else if (rssi > -85) strength = 1;
      else strength = 0;
    }
    drawWifiStrength(strength);
    display_ssd1306.drawLine(0, 20, SCREEN_WIDTH, 20, getDisplayWhite());
    display_ssd1306.setTextSize(1);
    const char* serverStatus;
    if (sslCertificateError) {
      serverStatus = "ZERT FEHLER";
    } else if (authenticationError) {
      serverStatus = "AUTH FEHLER";
    } else if (connectionError) {
      serverStatus = "CONN ERROR";
    } else {
      serverStatus = "Client ONLINE";
    }
    int16_t x1, y1;
    uint16_t w, h;
    display_ssd1306.getTextBounds(serverStatus, 0, 0, &x1, &y1, &w, &h);
    display_ssd1306.setCursor((SCREEN_WIDTH - w) / 2, 22);
    display_ssd1306.print(serverStatus);
    display_ssd1306.drawLine(0, 32, SCREEN_WIDTH, 32, getDisplayWhite());
    drawRXTXRects();
    display_ssd1306.display();
  } else {
    display_sh1106.clearDisplay();
    display_sh1106.setTextColor(getDisplayWhite());
    display_sh1106.setTextSize(2);
    display_sh1106.setCursor(0,1);
    display_sh1106.print(callsign);
    int rssi = WiFi.RSSI();
    int strength = 0;
    if (WiFi.status() == WL_CONNECTED) {
      if (rssi > -55) strength = 4;
      else if (rssi > -65) strength = 3;
      else if (rssi > -75) strength = 2;
      else if (rssi > -85) strength = 1;
      else strength = 0;
    }
    drawWifiStrength(strength);
    display_sh1106.drawLine(0, 20, SCREEN_WIDTH, 20, getDisplayWhite());
    display_sh1106.setTextSize(1);
    const char* serverStatus;
    if (sslCertificateError) {
      serverStatus = "ZERT FEHLER";
    } else if (authenticationError) {
      serverStatus = "AUTH FEHLER";
    } else if (connectionError) {
      serverStatus = "CONN ERROR";
    } else {
      serverStatus = "Client ONLINE";
    }
    int16_t x1, y1;
    uint16_t w, h;
    display_sh1106.getTextBounds(serverStatus, 0, 0, &x1, &y1, &w, &h);
    display_sh1106.setCursor((SCREEN_WIDTH - w) / 2, 22);
    display_sh1106.print(serverStatus);
    display_sh1106.drawLine(0, 32, SCREEN_WIDTH, 32, getDisplayWhite());
    drawRXTXRects();
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
              <p><strong>Display Type:</strong> <span id="displayType">-</span></p>
              <p><strong>SSL Validation:</strong> <span id="sslValidation">-</span></p>
            </div>
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
      <pre id="monitorArea" style="height:350px;overflow:auto;"></pre>
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
            document.getElementById('displayType').textContent = data.system.displayType || '-';
            document.getElementById('sslValidation').textContent = data.system.sslValidation || '-';
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
        
        window.onload = function() {
          updateMonitor();
          updateHardwareInfo();
        };
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
  wifiSsid[63]=0; wifiPass[63]=0; serverUrl[63]=0; callsign[31]=0; otaRepoUrl[127]=0;
  saveConfig();
  appendMonitor("Konfiguration gespeichert. Neustart folgt.", "INFO");
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
  delay(500);
  ESP.restart();
}

void handleMonitor() {
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
  
  // Uptime calculation
  unsigned long uptimeMs = millis();
  unsigned long days = uptimeMs / (1000 * 60 * 60 * 24);
  unsigned long hours = (uptimeMs % (1000 * 60 * 60 * 24)) / (1000 * 60 * 60);
  unsigned long minutes = (uptimeMs % (1000 * 60 * 60)) / (1000 * 60);
  unsigned long seconds = (uptimeMs % (1000 * 60)) / 1000;
  String uptime = String(days) + "d " + String(hours) + "h " + String(minutes) + "m " + String(seconds) + "s";
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
  
  // Display type
  String dispType = (displayType == DISPLAY_SH1106G) ? "SH1106G" : "SSD1306";
  json += "\"displayType\":\"" + dispType + "\",";
  
  // SSL Validation
  json += "\"sslValidation\":\"" + String(sslValidation ? "Enabled" : "Disabled") + "\"";
  json += "}";
  
  json += "}";
  
  server.send(200, "application/json", json);
}

void startWebserver() {
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/monitor", handleMonitor);
  server.on("/monitor_clear", handleMonitorClear);
  server.on("/api/hardware_info", handleHardwareInfo);
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

bool tryConnectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid, wifiPass);
  appendMonitor("WLAN Verbindung wird aufgebaut...", "INFO");
  for (int i = 0; i < 40; ++i) {
    if (WiFi.status() == WL_CONNECTED) {
      appendMonitor("WLAN verbunden: " + WiFi.localIP().toString(), "INFO");
      return true;
    }
    blinkLED();
    delay(250);
  }
  appendMonitor("WLAN Verbindung fehlgeschlagen!", "ERROR");
  return false;
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

  loadConfig();
  
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
    }
  } else {
    appendMonitor("Watchdog Init fehlgeschlagen, Code: " + String(result) + " - System läuft ohne Watchdog", "WARNING");
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
  
  // **OPTIMIERT: Memory-Überwachung (weniger häufig)**
  if (now - lastMemoryCheck > MEMORY_CHECK_INTERVAL) {
    size_t freeHeap = ESP.getFreeHeap();
    size_t minHeap = ESP.getMinFreeHeap();
    size_t stackFree = uxTaskGetStackHighWaterMark(NULL);
    unsigned long uptimeSeconds = millis() / 1000;
    
    appendMonitor("System OK - Heap:" + String(freeHeap) + "B Stack:" + String(stackFree) + " Uptime:" + String(uptimeSeconds) + "s", "INFO");
    
    if (freeHeap < 10000) { // Unter 10KB freier Speicher
      appendMonitor("KRITISCH: Wenig freier Speicher! Neustart wird eingeleitet.", "ERROR");
      delay(1000);
      ESP.restart();
    }
    lastMemoryCheck = now;
  }
  
  server.handleClient();

  static unsigned long lastOled = 0;
  if (millis() - lastOled > 500) { // **OPTIMIERT: 500ms Intervall für Stabilität**
    updateOLED();
    lastOled = millis();
  }

  // Senden: RS232 -> HTTP POST zum Server
  if (RS232.available()) {
    String sdata = "";
    sdata.reserve(256); // Reserviere Speicher für bessere Performance
    
    while (RS232.available()) {
      char c = RS232.read();
      sdata += c;
      lastTX = millis();
    }
    
    if(sdata.length() > 0 && strlen(serverUrl) > 0) {
      HTTPClient http;
      
      String url = String(serverUrl) + "/api/senddata.php";
      configureHTTPClient(http, url);
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      
      String postData;
      postData.reserve(sdata.length() + 50);
      postData = "callsign=" + String(callsign) + "&data=" + sdata;
      
      int httpCode = http.POST(postData);
      
      if(httpCode == 200) {
        String response = http.getString();
        if(response == "DENY") {
          appendMonitor("Server verweigert Verbindung - Callsign nicht autorisiert!", "ERROR");
          authenticationError = true;
          sslCertificateError = false;
          connectionError = false;
        } else {
          appendMonitor("Server-Antwort: " + response, "DEBUG");
          authenticationError = false;
          sslCertificateError = false;
          connectionError = false;
        }
      } else if(httpCode == 403) {
        appendMonitor("Authentifizierung fehlgeschlagen - Callsign nicht autorisiert!", "ERROR");
        authenticationError = true;
        sslCertificateError = false;
        connectionError = false;
      } else if(httpCode < 0) {
        String url = String(serverUrl) + "/api/senddata.php";
        if(url.startsWith("https://") && sslValidation) {
          appendMonitor("SSL-Zertifikat Validierungsfehler: " + String(httpCode), "ERROR");
          sslCertificateError = true;
          authenticationError = false;
          connectionError = false;
        } else {
          appendMonitor("HTTP Timeout oder Verbindungsfehler: " + String(httpCode), "ERROR");
          authenticationError = false;
          sslCertificateError = false;
          connectionError = true;
        }
      } else {
        appendMonitor("HTTP POST Fehler: " + String(httpCode), "ERROR");
        authenticationError = false;
        sslCertificateError = false;
        connectionError = true;
      }
      
      http.end(); // **WICHTIG: HTTPClient korrekt schließen**
    }
  }

  // Smart-Polling: Adaptives HTTP-Polling für Hoststar
  static unsigned long lastFetch = 0;
  if (millis() - lastFetch > smartPollInterval && strlen(serverUrl) > 0) {
    
    // **OPTIMIERT: Task-Kooperation für Stabilität**
    yield();
    
    HTTPClient http;
    
    String url = String(serverUrl) + "/api/smart_getdata.php?callsign=" + String(callsign);
    configureHTTPClient(http, url);
    
    appendMonitor("Smart-Poll: " + url, "DEBUG");
    
    int httpCode = http.GET();
    
    if(httpCode == 200) {
      String response = http.getString();
      
      // Parse JSON Response für Smart-Polling
      if (response.startsWith("{")) {
        // Extrahiere "data" Feld
        int dataStart = response.indexOf("\"data\":\"");
        if (dataStart > 0) {
          dataStart += 8; // Skip "data":"
          int dataEnd = response.indexOf("\"", dataStart);
          if (dataEnd > dataStart) {
            String base64Data = response.substring(dataStart, dataEnd);
            
            if (base64Data.length() > 0) {
              // Dekodiere Base64-Daten
              String decodedData = decodeBase64Simple(base64Data);
              if (decodedData.length() > 0) {
                // Debug: HEX-Anzeige der Rohdaten
                String hexData;
                hexData.reserve(decodedData.length() * 3); // Speicher reservieren
                for (int i = 0; i < decodedData.length(); i++) {
                  char hex[4];
                  sprintf(hex, "%02X", (unsigned char)decodedData[i]);
                  hexData += hex;
                  if (i < decodedData.length() - 1) hexData += " ";
                }
                appendMonitor("[SMART] RAW HEX: " + hexData, "DEBUG");
                
                // RS232 ausgeben (bereits mit korrekten Zeilenendezeichen vom Server)
                RS232.print(decodedData);
                RS232.flush(); // Sicherstellen dass Daten gesendet werden
                lastRX = millis();
                appendMonitor("[SMART] Empfangen: " + String(decodedData.length()) + " bytes", "INFO");
                appendMonitor("[SMART] KISS: " + decodeKissFrame(decodedData), "DEBUG");
              }
            }
          }
        }
        
        // Extrahiere nächstes Polling-Intervall
        int pollStart = response.indexOf("\"next_poll_seconds\":");
        if (pollStart > 0) {
          pollStart += 20; // Skip "next_poll_seconds":
          int pollEnd = response.indexOf(",", pollStart);
          if (pollEnd == -1) pollEnd = response.indexOf("}", pollStart);
          
          if (pollEnd > pollStart) {
            String pollString = response.substring(pollStart, pollEnd);
            float nextPollSecondsFloat = pollString.toFloat();
            if (nextPollSecondsFloat > 0.0 && nextPollSecondsFloat <= 2.0) { // Max 2 Sekunden
              smartPollInterval = max((unsigned long)(nextPollSecondsFloat * 1000), 500UL); // Min 0.5s, Max 2s
              appendMonitor("[SMART] Nächstes Poll: " + String(nextPollSecondsFloat, 1) + "s", "DEBUG");
            }
          }
        }
        
        authenticationError = false;
        sslCertificateError = false;
        connectionError = false;
      } else {
        // Alte Format-Kompatibilität
        if(response.length() > 0 && response != "{\"error\":\"DENY\"}") {
          RS232.print(response);
          RS232.flush(); // Daten senden (keine zusätzlichen \r\n)
          lastRX = millis();
          appendMonitor("Legacy-Format empfangen: " + response, "INFO");
        }
      }
      
    } else if(httpCode == 403) {
      appendMonitor("Smart-Polling: Authentifizierung fehlgeschlagen!", "ERROR");
      authenticationError = true;
      sslCertificateError = false; // Reset SSL-Fehler
      connectionError = false; // Reset Verbindungsfehler
      smartPollInterval = 30000; // Bei Fehlern langsamer prüfen
    } else if(httpCode < 0) {
      // Negative HTTP-Codes sind meist SSL/TLS-Fehler
      if (String(serverUrl).startsWith("https://") && sslValidation) {
        appendMonitor("Smart-Polling SSL-Zertifikatsfehler: " + String(httpCode), "ERROR");
        sslCertificateError = true;
        authenticationError = false;
        connectionError = false;
      } else {
        appendMonitor("Smart-Polling Timeout/Verbindungsfehler: " + String(httpCode), "ERROR");
        sslCertificateError = false;
        authenticationError = false;
        connectionError = true; // Verbindungsfehler setzen
      }
      smartPollInterval = min(smartPollInterval + 5000, 30000UL); // Graduell verlangsamen
    } else {
      appendMonitor("Smart-Polling Fehler: " + String(httpCode), "ERROR");
      authenticationError = false;
      sslCertificateError = false;
      connectionError = true; // Allgemeine HTTP-Fehler als Verbindungsfehler behandeln
      smartPollInterval = min(smartPollInterval + 5000, 30000UL); // Graduell verlangsamen
    }
    
    http.end(); // **WICHTIG: HTTPClient korrekt schließen**
    lastFetch = millis();
    
    // **OPTIMIERT: Task-Kooperation für Stabilität**
    yield();
  }
}

void blinkLED() {
  unsigned long now = millis();
  if (now - lastBlink > 250) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    lastBlink = now;
  }
}

// KISS-Frame-Dekodierung für Debug-Anzeige
String decodeKissFrame(String rawData) {
  if (rawData.length() < 3) return "Invalid KISS frame";
  
  String result = "";
  int pos = 0;
  
  // KISS-Header prüfen
  if ((unsigned char)rawData[0] == 0xC0 && (unsigned char)rawData[1] == 0x00) {
    result += "KISS: ";
    pos = 2; // Skip KISS header
  }
  
  // AX25-Header dekodieren
  if (rawData.length() >= pos + 14) {
    // Destination (6 bytes)
    String dest = "";
    for (int i = 0; i < 6; i++) {
      char c = ((unsigned char)rawData[pos + i]) >> 1;
      if (c != ' ') dest += c;
    }
    pos += 7; // Skip destination + SSID
    
    // Source (6 bytes)  
    String src = "";
    for (int i = 0; i < 6; i++) {
      char c = ((unsigned char)rawData[pos + i]) >> 1;
      if (c != ' ') src += c;
    }
    pos += 7; // Skip source + SSID
    
    result += src + ">" + dest + ": ";
    
    // Control + PID überspringen
    pos += 2;
    
    // Information field (bis zum Ende oder CR)
    String info = "";
    while (pos < rawData.length()) {
      char c = rawData[pos];
      if (c == 0x0D || c == (char)0xC0) break; // Stop bei CR oder KISS-Ende
      if (c >= 32 && c <= 126) info += c; // Nur druckbare Zeichen
      pos++;
    }
    result += info;
  } else {
    result += "Short frame";
  }
  
  return result;
}

// Base64-Dekodierung für Smart-Polling (korrekte Implementierung)
String decodeBase64Simple(String input) {
  String output = "";
  
  // Base64 Zeichen-Tabelle
  const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  
  // Zähle Padding-Zeichen für korrekte Längenberechnung
  int paddingCount = 0;
  String originalInput = input;
  while (input.endsWith("=")) {
    input.remove(input.length() - 1);
    paddingCount++;
  }
  
  int inputLen = input.length();
  if (inputLen == 0) return "";
  
  // Berechne die erwartete Ausgabelänge
  int expectedOutputLen = (originalInput.length() * 3) / 4 - paddingCount;
  
  // Verarbeite 4-Zeichen-Blöcke
  for (int i = 0; i < inputLen; i += 4) {
    uint32_t sextet_a = 0, sextet_b = 0, sextet_c = 0, sextet_d = 0;
    
    // Konvertiere Zeichen zu Werten
    char c1 = (i < inputLen) ? input.charAt(i) : 'A';
    char c2 = (i + 1 < inputLen) ? input.charAt(i + 1) : 'A';
    char c3 = (i + 2 < inputLen) ? input.charAt(i + 2) : 'A';
    char c4 = (i + 3 < inputLen) ? input.charAt(i + 3) : 'A';
    
    // Finde Index in der Base64-Tabelle
    for (int j = 0; j < 64; j++) {
      if (base64_chars[j] == c1) sextet_a = j;
      if (base64_chars[j] == c2) sextet_b = j;
      if (base64_chars[j] == c3) sextet_c = j;
      if (base64_chars[j] == c4) sextet_d = j;
    }
    
    // Kombiniere die 6-Bit-Werte zu 24 Bits
    uint32_t triple = (sextet_a << 18) + (sextet_b << 12) + (sextet_c << 6) + sextet_d;
    
    // Extrahiere nur so viele Bytes wie erwartet
    if (output.length() < expectedOutputLen) output += (char)((triple >> 16) & 0xFF);
    if (output.length() < expectedOutputLen) output += (char)((triple >> 8) & 0xFF);
    if (output.length() < expectedOutputLen) output += (char)(triple & 0xFF);
  }
  
  return output;
}

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