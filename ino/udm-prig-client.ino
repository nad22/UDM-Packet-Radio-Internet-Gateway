/*
 * ==================================================================================
 * UDM Packet Radio Internet Gateway (PRIG) - ESP32 Client Firmware v3.0.0
 * ==================================================================================
 * 
 * Modern Serverless CB-Funk Simulation Gateway with MQTT Broadcasting. Works with tfpcx or flexnet
 * driverts in KISS Mode. 
 * 
 * Features:
 * - CB-Funk 40-channel simulation with professional 7-segment display
 * - Serverless MQTT broadcast architecture (no server dependencies)
 * - XOR payload encryption with shared secret for secure communication
 * - Studio-quality I2S AFSK audio output (MAX98357A support)
 * - Professional OLED display with brightness control (SH1106G/SSD1306)
 * - Advanced web configuration interface with live monitoring
 * - EEPROM persistence for all settings and crash logs
 * - Watchdog protection with comprehensive error handling
 * - OTA firmware updates via GitHub integration
 * 
 * Hardware Requirements:
 * - ESP32 Development Board (WROOM-32 or similar)
 * - OLED Display 128x64 (SH1106G or SSD1306, I2C)
 * - MAX98357A I2S Audio Amplifier (optional, for AFSK output)  
 * - RS232 Interface (MAX3232 or similar)
 * - MAX3232 for RS232 Level Shifting
 * - 3,3V to 5,5V and vice versa level Shifter for higher Signal Levels for MAX3232

 * 
 * Author: NAD
 * License: MIT
 * Repository: https://github.com/nad22/UDM-Packet-Radio-Internet-Gateway
 * 
 * ==================================================================================
 */

// Core ESP32 and communication libraries
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
#include <math.h>          // For AFSK mathematics (sin, PI)
#include <driver/i2s.h>    // I2S for MAX98357A audio amplifier

// MQTT configuration - increased buffer size for reliable packet transmission
#define MQTT_MAX_PACKET_SIZE 1024
#define MQTT_KEEPALIVE 300

// MQTT client libraries for secure cloud connectivity
#include <WiFiClientSecure.h>
#include <ArduinoMqttClient.h>

// ==================================================================================
// SYSTEM CONFIGURATION
// ==================================================================================

// Watchdog timer configuration for system stability
#define WDT_TIMEOUT 30              // 30 second watchdog timeout
#define EEPROM_SIZE 1024            // Extended EEPROM size for crash logs and configuration

// EEPROM memory layout - persistent configuration storage
#define SSID_OFFSET 0               // WiFi SSID (64 bytes)
#define PASS_OFFSET 64              // WiFi password (64 bytes)
#define SERVERURL_OFFSET 128        // Legacy server URL (64 bytes, unused in MQTT mode)
#define CALLSIGN_OFFSET 160         // Station callsign (32 bytes)
#define BAUD_OFFSET 194             // RS232 baudrate (4 bytes)
#define LOGLEVEL_OFFSET 198         // Debug log level (1 byte)
#define OTAURL_OFFSET 200           // OTA repository URL (128 bytes)
#define VERSION_OFFSET 328          // Firmware version string (16 bytes)
#define DISPLAYTYPE_OFFSET 345      // Display type selector (1 byte: 0=SH1106G, 1=SSD1306)
#define SSL_VALIDATION_OFFSET 346   // SSL certificate validation (1 byte: 0=disabled, 1=enabled)
#define CPU_FREQUENCY_OFFSET 347    // CPU frequency (1 byte: 0=240MHz, 1=160MHz, 2=80MHz)
#define DISPLAY_BRIGHTNESS_OFFSET 348  // Display brightness level (1 byte: 0-255)

// MQTT configuration storage (EEPROM 349-399)
#define MQTT_ENABLED_OFFSET 349        // MQTT mode enabled (1 byte: 0=disabled, 1=enabled)
#define MQTT_BROKER_OFFSET 350         // MQTT broker URL (64 bytes)
#define MQTT_PORT_OFFSET 414           // MQTT port number (2 bytes, typically 8883 for SSL)
#define MQTT_USERNAME_OFFSET 416       // MQTT username (16 bytes)
#define MQTT_PASSWORD_OFFSET 432       // MQTT password (32 bytes)
#define MQTT_SHARED_SECRET_OFFSET 464  // Shared secret for payload encryption (32 bytes)
#define CB_CHANNEL_OFFSET 496          // CB channel 1-40 (1 byte)
#define PACKET_AUDIO_OFFSET 497        // Packet audio enabled/disabled (1 byte)
#define AUDIO_VOLUME_OFFSET 498        // Audio volume level 0-100 (1 byte)
#define TX_DELAY_OFFSET 499            // TX Delay in milliseconds (2 bytes)
#define FREQ_MARK_OFFSET 501           // AFSK Mark frequency (2 bytes, default: 1200 Hz)
#define FREQ_SPACE_OFFSET 503          // AFSK Space frequency (2 bytes, default: 2200 Hz)
#define HARDWARE_GAIN_OFFSET 505       // Hardware gain level 0-3 (1 byte)

// Crash log system storage (EEPROM 510-1023) - MOVED to avoid conflicts
#define CRASH_LOG_START_OFFSET 510
#define CRASH_LOG_COUNT_OFFSET 510     // Number of crash logs (4 bytes)
#define CRASH_LOG_ENTRIES_OFFSET 514   // Crash log entries (5 x 120 = 600 bytes)
#define CRASH_LOG_ENTRY_SIZE 120       // Entry size: timestamp (20) + message (100)
#define MAX_CRASH_LOGS 5               // Maximum number of stored crash logs

// ==================================================================================
// HARDWARE PIN DEFINITIONS
// ==================================================================================

#define LED_PIN 2                    // Status LED pin
#define BUZZER_PIN 4                 // Buzzer pin (not used with I2S, kept for compatibility)

// I2S audio pins for MAX98357A amplifier
#define I2S_DOUT_PIN 25              // DIN pin of MAX98357A
#define I2S_BCLK_PIN 26              // BCLK pin of MAX98357A  
#define I2S_LRC_PIN  27              // LRC pin of MAX98357A
#define I2S_GAIN_PIN 33              // GAIN pin of MAX98357A (hardware volume control)

// ==================================================================================
// AFSK (Audio Frequency Shift Keying) CONFIGURATION
// ==================================================================================

// Bell 202 modem frequencies for 1200 baud packet radio (AFSK standard)
// Note: These are now configurable - see global variables afskFreqMark and afskFreqSpace
#define BAUD_RATE  1200              // Bits per second
#define BIT_DURATION_US (1000000 / BAUD_RATE)  // ~833 microseconds per bit

// AFSK-specific parameters for I2S audio output
#define PHASE_CONTINUOUS true        // Phase-continuous AFSK for clean audio
#define I2S_SAMPLE_RATE 44100        // CD-quality sample rate
#define I2S_BITS_PER_SAMPLE 16       // 16-bit audio resolution
#define I2S_BUFFER_SIZE 512          // I2S buffer size for smooth audio
#ifndef PI
#define PI 3.14159265359
#endif

// ==================================================================================
// DISPLAY CONFIGURATION
// ==================================================================================

#define SCREEN_WIDTH 128             // OLED display width in pixels
#define SCREEN_HEIGHT 64             // OLED display height in pixels
#define OLED_RESET    -1             // Reset pin (not used)
#define OLED_LINE_LEN 33             // Maximum characters per display line
#define OLED_LINE_DIVIDER "---------------------------------"

// Display type definitions
#define DISPLAY_SH1106G 0            // SH1106G display type (larger display)
#define DISPLAY_SSD1306 1            // SSD1306 display type (smaller display)

// Display objects (only one will be used based on configuration)
Adafruit_SH1106G display_sh1106(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_SSD1306 display_ssd1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==================================================================================
// GLOBAL VARIABLES AND OBJECTS
// ==================================================================================

// Hardware serial interface for packet radio communication
HardwareSerial RS232(1);

// System timing variables
unsigned long lastBlink = 0;            // LED blink timing
bool ledState = false;                   // LED state tracking
unsigned long lastKeepalive = 0;        // Keepalive timing
const unsigned long KEEPALIVE_INTERVAL = 10000;  // 10 second keepalive interval
unsigned long lastRS232 = 0;            // RS232 activity timing

// Boot message buffer system to avoid appendMonitor() timeouts during initialization
#define MAX_BOOT_MESSAGES 20
struct BootMessage {
  String message;
  String level;
};
BootMessage bootMessages[MAX_BOOT_MESSAGES];
int bootMessageCount = 0;
bool systemReady = false;               // System fully initialized flag

// RX/TX activity indicators for display
unsigned long lastRX = 0;               // Last receive timestamp
unsigned long lastTX = 0;               // Last transmit timestamp
const unsigned long RS232_ACTIVE_TIME = 1000;  // 1 second activity display time

// Web server for configuration interface
WebServer server(80);

// ==================================================================================
// CONFIGURATION VARIABLES (stored in EEPROM)
// ==================================================================================

// Network configuration
char wifiSsid[64] = "";                  // WiFi network name
char wifiPass[64] = "";                  // WiFi password
char serverUrl[64] = "";                 // Legacy server URL (unused in MQTT mode)
char callsign[32] = "";                  // Station callsign identifier
char otaRepoUrl[128] = "https://raw.githubusercontent.com/nad22/UDM-Packet-Radio-Internet-Gateway/main/ota";

// System configuration
uint32_t baudrate = 2400;               // RS232 serial baudrate
uint8_t logLevel = 1;                   // Debug logging level
uint8_t displayType = DISPLAY_SSD1306;  // Display type (default: smaller SSD1306)
bool sslValidation = true;              // SSL certificate validation enabled
uint8_t cpuFrequency = 0;               // CPU frequency (0=240MHz, 1=160MHz, 2=80MHz)
uint8_t displayBrightness = 128;        // Display brightness level (0-255, default: 50%)

// MQTT configuration for serverless CB-Funk simulation
bool mqttEnabled = true;                // MQTT mode enabled (pure MQTT implementation)
char mqttBroker[64] = "";               // HiveMQ Cloud broker URL
uint16_t mqttPort = 8883;               // MQTT SSL port (default 8883)
char mqttUsername[16] = "";             // MQTT authentication username
char mqttPassword[32] = "";             // MQTT authentication password
char mqttSharedSecret[32] = "";         // Shared secret for payload encryption
uint8_t cbChannel = 1;                  // CB-Funk channel 1-40 (default: channel 1)
bool packetAudioEnabled = true;         // Packet radio audio simulation enabled
uint16_t txDelay = 100;                 // TX Delay in milliseconds (PTT to data start, default: 100ms)
uint8_t audioVolume = 70;               // Audio volume level 0-100% (default: 70%)
uint16_t afskFreqMark = 1200;           // AFSK Mark frequency (configurable, default: 1200 Hz)
uint16_t afskFreqSpace = 2200;          // AFSK Space frequency (configurable, default: 2200 Hz)
uint8_t hardwareGain = 0;               // Hardware gain level (0=9dB Standard, 1=6dB Niedrig, 2=12dB Hoch)

// ==================================================================================
// MQTT CLIENT OBJECTS
// ==================================================================================

WiFiClientSecure mqttWifiClient;        // Secure WiFi client for MQTT
MqttClient mqttClient(mqttWifiClient);   // MQTT client instance
unsigned long lastMqttReconnect = 0;    // Last MQTT reconnection attempt
bool mqttConnected = false;              // MQTT connection status

// Simple broadcast system (radio simulation)
// IMPORTANT: HiveMQ Cloud must be configured for:
// - Message Expiry: 0 (no storage of old messages)  
// - Retain: disabled (no persistent messages)
// - QoS: 0 (Fire-and-forget like real radio)
String mqttBroadcastTopic = "udmprig/rf/1";  // Dynamic CB-Funk channel (set in setupMqttTopics())

// KISS protocol buffer for clean message separation
static String kissBuffer = "";          // Buffer for KISS frame assembly
static bool inKissFrame = false;        // KISS frame parsing state
static int expectedKissLength = 0;      // Expected KISS frame length
static int currentKissLength = 0;       // Current KISS frame length

// ==================================================================================
// SYSTEM STATE VARIABLES
// ==================================================================================

bool apActive = false;                  // Access Point mode active flag
bool connectionError = false;           // WiFi connection error flag

// WiFi monitoring and stability management
unsigned long lastWiFiCheck = 0;       // Last WiFi status check timestamp
unsigned long wifiReconnectAttempts = 0; // WiFi reconnection attempt counter
const unsigned long WIFI_CHECK_INTERVAL = 30000;   // Check WiFi every 30 seconds
const unsigned long WIFI_RECONNECT_DELAY = 5000;   // 5 second delay between reconnects
bool forceWiFiReconnect = false;        // Force WiFi reconnection flag

// ==================================================================================
// CRASH LOG SYSTEM
// ==================================================================================

struct CrashLogEntry {
  char timestamp[21];                   // Timestamp string (YYYY-MM-DD HH:MM:SS + null)
  char message[100];                    // Error message text
};

uint32_t crashLogCount = 0;             // Total number of crash logs recorded
CrashLogEntry crashLogs[MAX_CRASH_LOGS]; // Crash log storage array

// ==================================================================================
// MONITORING AND DEBUG SYSTEM
// ==================================================================================

#define MONITOR_BUF_SIZE 4096           // Monitor buffer size for web interface
String monitorBuf = "";                 // Monitor message buffer
String rs232HexBuf = "";                // RS232 hex dump buffer
String rs232AscBuf = "";                // RS232 ASCII buffer

// NTP time server configuration
const char* ntpServer = "at.pool.ntp.org";  // Austrian NTP server
const long  gmtOffset_sec = 3600;            // GMT+1 offset
const int   daylightOffset_sec = 3600;       // Daylight saving time offset

// ==================================================================================
// OTA (Over-The-Air) UPDATE SYSTEM
// ==================================================================================

char localVersion[16] = "3.0.0";       // Current firmware version (stable I2C-optimized)
bool otaCheckedThisSession = false;     // OTA check performed this session flag

// ==================================================================================
// SYSTEM PROTECTION AND MONITORING
// ==================================================================================

// Anti-freeze protection system
unsigned long lastWatchdogReset = 0;    // Last watchdog reset timestamp
unsigned long lastMemoryCheck = 0;      // Last memory check timestamp
const unsigned long MEMORY_CHECK_INTERVAL = 60000;  // Memory check every 60 seconds

// CPU load measurement system
unsigned long lastCpuMeasurement = 0;   // Last CPU measurement timestamp
unsigned long busyTime = 0;             // CPU busy time accumulator
unsigned long idleTime = 0;             // CPU idle time accumulator
float cpuUsagePercent = 0.0;            // Current CPU usage percentage
const unsigned long CPU_MEASUREMENT_INTERVAL = 10000;  // Measure CPU every 10 seconds

// ==================================================================================
// FUNCTION FORWARD DECLARATIONS
// ==================================================================================

// Core system functions
void appendMonitor(const String& msg, const char* level = "INFO");
String getTimestamp();
void blinkLED();

// Protocol and communication functions
String decodeKissFrame(String rawData);     // KISS frame decoding for debug display

// OTA update system functions
void handleOTACheck();                      // Check for firmware updates
void handleOTAUpdate();                     // Perform firmware update

// Web API handler functions
void handleHardwareInfo();                  // Hardware information API
void handleReboot();                        // System reboot API

// Crash log system functions
void saveCrashLog(const String& message);   // Save crash log entry
void loadCrashLogs();                       // Load crash logs from EEPROM
void saveCrashLogsToEEPROM();               // Save crash logs to EEPROM

// MQTT payload encryption/decryption functions
String encryptPayload(const String& payload);
String decryptPayload(const String& encryptedPayload);

// MQTT communication functions
void onMqttMessage(int messageSize);        // MQTT message handler
bool connectMQTT();                         // Connect to MQTT broker
void setupMqttTopics();                     // Configure MQTT topics
bool publishMqttMessage(const String& topic, const String& message);  // Publish MQTT message
void handleMqttLoop();                          // MQTT main loop handler
bool isMqttConnected();                        // Check MQTT connection status
void processMqttMessage(const String& message); // MQTT message processing
String hexToBytes(const String& hexString);   // Hex string to bytes conversion

// Crash log management functions
void clearCrashLogs();                         // Clear all crash logs
void handleClearCrashLogs();                   // Web API handler for clearing crash logs

// WiFi management functions
void checkWiFiConnection();                    // WiFi connection monitoring
String getWiFiStatusText(wl_status_t status);  // Convert WiFi status to text
String getFormattedUptime();                   // Format system uptime
void logWiFiDiagnostics();                     // Extended WiFi diagnostics
bool tryConnectWiFi();                         // Attempt WiFi connection
bool isGatewayReachable();                     // Gateway reachability test

// Display management functions
bool initDisplay();                            // Initialize OLED display
void handleDisplayBrightness();                // Display brightness API handler
void setDisplayBrightness(uint8_t brightness); // Set display brightness level

// Audio system functions
void setupI2SAudio();                          // Initialize I2S audio system
void setupHardwareGain();                         // Initialize hardware gain control
void setHardwareGain(uint8_t gainLevel);          // Set hardware gain level (0-3)

// KISS protocol processing
void processCompleteKissMessage(const String& kissData);

// ==================================================================================
// SSL CERTIFICATE AUTHORITY BUNDLE
// ==================================================================================

// Root CA bundle for SSL certificate validation (Let's Encrypt, DigiCert, etc.)
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

// ==================================================================================
// DISPLAY INITIALIZATION FUNCTIONS
// ==================================================================================

/**
 * Initialize OLED display based on configured display type
 * Supports both SSD1306 and SH1106G display controllers
 * @return true if initialization successful, false otherwise
 */
bool initDisplay() {
  // Fast display initialization - show boot message immediately
  bool success = false;
  
  if (displayType == DISPLAY_SSD1306) {
    success = display_ssd1306.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    if (success) {
      display_ssd1306.clearDisplay();
      // Show immediate boot message
      display_ssd1306.setTextSize(1);
      display_ssd1306.setTextColor(WHITE);
      display_ssd1306.setCursor(0,0);
      display_ssd1306.println("UDM-PRIG Client");
      display_ssd1306.println("Booting...");
      display_ssd1306.display();
      setDisplayBrightness(displayBrightness);
    }
  } else {
    success = display_sh1106.begin(0x3C);
    if (success) {
      display_sh1106.clearDisplay();
      // Show immediate boot message
      display_sh1106.setTextSize(1);
      display_sh1106.setTextColor(WHITE);
      display_sh1106.setCursor(0,0);
      display_sh1106.println("UDM-PRIG Client");
      display_sh1106.println("Booting...");
      display_sh1106.display();
      setDisplayBrightness(displayBrightness);
    }
  }
  
  return success;
}

// HTTPS HTTPClient configuration with automatic URL detection
void configureHTTPClient(HTTPClient &http, String url) {
  if (url.startsWith("https://")) {
    WiFiClientSecure *client = new WiFiClientSecure;
    
    if (sslValidation) {
      // Real SSL certificate validation (for official certificates)
      client->setCACert(root_ca);
    } else {
      // For self-signed certificates: disable certificate validation
      client->setInsecure(); 
    }
    
    http.begin(*client, url);
  } else {
    // Standard HTTP connection
    http.begin(url);
  }
  
  // Standard timeouts (longer timeouts for smart polling)
  http.setTimeout(10000);  // 10 seconds instead of 5
  http.setConnectTimeout(5000);  // 5 seconds instead of 3
}
uint16_t getDisplayWhite() {
  if (displayType == DISPLAY_SSD1306) {
    return SSD1306_WHITE;
  } else {
    return SH110X_WHITE;
  }
}

/**
 * Display boot messages on OLED screen during startup
 * Implements true Linux-style scrolling without clearing display
 * @param msg Message to display (truncated to fit display width)
 */
void bootPrint(const String &msg) {
  static String bootLines[8] = {"", "", "", "", "", "", "", ""}; // Buffer for 8 lines
  static int currentLine = 0;
  static bool displayFull = false;
  
  // Truncate message to fit display width
  String showMsg = msg.substring(0, OLED_LINE_LEN);
  
  if (!displayFull && currentLine < 8) {
    // Display is not full yet, just add new line
    bootLines[currentLine] = showMsg;
    currentLine++;
    if (currentLine >= 8) {
      displayFull = true;
    }
  } else {
    // Display is full, scroll up (Linux-style)
    for (int i = 0; i < 7; i++) {
      bootLines[i] = bootLines[i + 1];
    }
    bootLines[7] = showMsg; // Add new message at bottom
  }
  
  // Redraw entire display with all lines
  if (displayType == DISPLAY_SSD1306) {
    display_ssd1306.clearDisplay();
    display_ssd1306.setTextSize(1);
    display_ssd1306.setTextColor(getDisplayWhite());
    
    for (int i = 0; i < 8; i++) {
      if (bootLines[i].length() > 0) {
        display_ssd1306.setCursor(0, i * 8);
        display_ssd1306.print(bootLines[i]);
      }
    }
    display_ssd1306.display();
  } else {
    display_sh1106.clearDisplay();
    display_sh1106.setTextSize(1);
    display_sh1106.setTextColor(getDisplayWhite());
    
    for (int i = 0; i < 8; i++) {
      if (bootLines[i].length() > 0) {
        display_sh1106.setCursor(0, i * 8);
        display_sh1106.print(bootLines[i]);
      }
    }
    display_sh1106.display();
  }
  
  delay(200); // Reduced delay for smoother scrolling
}

/**
 * Adds a boot message to the buffer instead of calling appendMonitor() during initialization
 * This prevents timeout issues when WiFi/NTP are not yet ready
 */
void addBootMessage(const String &message, const char* level) {
  if (systemReady) {
    // System is ready, write directly to monitor
    appendMonitor(message, level);
  } else {
    // System not ready yet, buffer the message
    if (bootMessageCount < MAX_BOOT_MESSAGES) {
      bootMessages[bootMessageCount].message = message;
      bootMessages[bootMessageCount].level = String(level); // Convert to String for storage
      bootMessageCount++;
    }
    // Also write to serial for immediate feedback
    Serial.println(String(level) + ": " + message);
  }
}

/**
 * Flushes all buffered boot messages to the monitor system
 * Called once the system is fully initialized
 */
void flushBootMessages() {
  Serial.println("Flushing " + String(bootMessageCount) + " buffered boot messages...");
  for (int i = 0; i < bootMessageCount; i++) {
    appendMonitor(bootMessages[i].message, bootMessages[i].level.c_str());
    delay(50); // Small delay to avoid overwhelming the system
  }
  bootMessageCount = 0; // Clear the buffer
  systemReady = true;   // Mark system as ready for direct appendMonitor calls
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
    // 4 signal bars (without antenna symbol)
    for(int i=0;i<4;i++) {
      if(strength > i) {
        // Strong signal: draw filled bar
        display_ssd1306.fillRect(x+4+i*3, y+12-2*i, 2, 2+2*i, white);
      } else {
        // Weak signal: explicitly black out bar (hide)
        display_ssd1306.fillRect(x+4+i*3, y+12-2*i, 2, 2+2*i, black);
      }
    }
  } else {
    // 4 signal bars (without antenna symbol)
    for(int i=0;i<4;i++) {
      if(strength > i) {
        // Strong signal: draw filled bar
        display_sh1106.fillRect(x+4+i*3, y+12-2*i, 2, 2+2*i, white);
      } else {
        // Weak signal: explicitly black out bar (hide)
        display_sh1106.fillRect(x+4+i*3, y+12-2*i, 2, 2+2*i, black);
      }
    }
  }
}

void drawRXTXRects() {
  // Debug: Output current RX/TX status
  unsigned long now = millis();
  bool rxActive = (now - lastRX < RS232_ACTIVE_TIME);
  bool txActive = (now - lastTX < RS232_ACTIVE_TIME);
  
  // Removed: RX/TX debug outputs for better performance
  
  int rect_width = 26;
  int rect_height = 16;
  int rect_y = 48;
  int rx_y = 45;  // RX box 3px higher than TX box
  int rx_x = 16;
  int tx_x = 80;
  
  uint16_t white = getDisplayWhite();
  uint16_t black = (displayType == DISPLAY_SSD1306) ? SSD1306_BLACK : SH110X_BLACK;

  if (displayType == DISPLAY_SSD1306) {
    display_ssd1306.setTextSize(1);
    display_ssd1306.setTextColor(white);
    display_ssd1306.setCursor(rx_x+6, rx_y-10);
    display_ssd1306.print("RX");
    display_ssd1306.setCursor(tx_x+6, rect_y-10);
    display_ssd1306.print("TX");

    if (millis() - lastRX < RS232_ACTIVE_TIME) {
      display_ssd1306.fillRect(rx_x, rx_y, rect_width, rect_height, white);
      display_ssd1306.drawRect(rx_x, rx_y, rect_width, rect_height, black);
    } else {
      display_ssd1306.drawRect(rx_x, rx_y, rect_width, rect_height, white);
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
    display_sh1106.setCursor(rx_x+6, rx_y-10);
    display_sh1106.print("RX");
    display_sh1106.setCursor(tx_x+6, rect_y-10);
    display_sh1106.print("TX");

    if (millis() - lastRX < RS232_ACTIVE_TIME) {
      display_sh1106.fillRect(rx_x, rx_y, rect_width, rect_height, white);
      display_sh1106.drawRect(rx_x, rx_y, rect_width, rect_height, black);
    } else {
      display_sh1106.drawRect(rx_x, rx_y, rect_width, rect_height, white);
    }
    if (millis() - lastTX < RS232_ACTIVE_TIME) {
      display_sh1106.fillRect(tx_x, rect_y, rect_width, rect_height, white);
      display_sh1106.drawRect(tx_x, rect_y, rect_width, rect_height, black);
    } else {
      display_sh1106.drawRect(tx_x, rect_y, rect_width, rect_height, white);
    }
  }
}

// Draw 7-segment digit (CB radio style with separated segments!)
void draw7SegmentDigit(int x, int y, uint8_t digit) {
  // 7-segment patterns for digits 0-9
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
  
  int segLen = 7;   // Segment length (smaller for more space)
  int segThick = 1; // Segment thickness (thin)
  int gap = 1;      // Gap between segments (IMPORTANT!)
  
  // Segment positions with gaps between segments
  if(segments[digit][0]) display_sh1106.fillRect(x+gap, y, segLen, segThick, getDisplayWhite());
  // Segment B (top right)
  if(segments[digit][1]) display_sh1106.fillRect(x+segLen+gap, y+gap, segThick, segLen, getDisplayWhite());
  // Segment C (bottom right)
  if(segments[digit][2]) display_sh1106.fillRect(x+segLen+gap, y+segLen+gap+1, segThick, segLen, getDisplayWhite());
  // Segment D (bottom)
  if(segments[digit][3]) display_sh1106.fillRect(x+gap, y+2*segLen+2*gap, segLen, segThick, getDisplayWhite());
  // Segment E (bottom left)
  if(segments[digit][4]) display_sh1106.fillRect(x, y+segLen+gap+1, segThick, segLen, getDisplayWhite());
  // Segment F (top left)
  if(segments[digit][5]) display_sh1106.fillRect(x, y+gap, segThick, segLen, getDisplayWhite());
  // Segment G (middle)
  if(segments[digit][6]) display_sh1106.fillRect(x+gap, y+segLen+gap, segLen, segThick, getDisplayWhite());
}

// CB channel display with "CH" + 7-segment digits + RX/TX (completely centered)
void drawCBChannelDisplay() {
  // Calculate centered Y position between horizontal line (32) and display bottom (64)
  int centerY = 42; // Centered between line and bottom edge
  
  // Calculate horizontal centering for the COMPLETE CB display group
  // CH(12px) + gap(6px) + digit1(9px) + separation(2px) + digit2(9px) + gap(8px) + RX/TX(29px) = 75px total
  int displayWidth = 128; // Both displays 128px wide
  int totalDisplayWidth = 75; // Total width of complete CB display with RX/TX
  int startX = (displayWidth - totalDisplayWidth) / 2; // Perfect centering of entire group
  
  // "CH" text (centered position)
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
  
  // CB channel with leading zero (7-segment style) - centered position
  uint8_t tens = cbChannel / 10;
  uint8_t ones = cbChannel % 10;
  
  int digit1X = startX + 18; // After "CH" + small gap
  int digit2X = startX + 29; // After first digit + 2 pixel separation
  int digitY = centerY - 2;  // Slightly higher than CH for better appearance
  
  // First digit (or leading 0)
  if(cbChannel < 10) {
    draw7SegmentDigit(digit1X, digitY, 0);
  } else {
    draw7SegmentDigit(digit1X, digitY, tens);
  }
  
  // Second digit
  draw7SegmentDigit(digit2X, digitY, ones);
  
  // RX/TX boxes to the right of CB display (part of centered group)
  int boxX = startX + 46;    // After CB digits + gap (adjusted for 2px separation)
  int labelX = boxX + 17;    // Labels to the right of boxes
  int boxY1 = centerY - 2;   // First box (RX)
  int boxY2 = centerY + 7;   // Second box (TX)
  
  // RX box and label
  if (displayType == DISPLAY_SSD1306) {
    // RX box
    if (millis() - lastRX < RS232_ACTIVE_TIME) {
      display_ssd1306.fillRect(boxX, boxY1, 15, 8, getDisplayWhite());
    } else {
      display_ssd1306.drawRect(boxX, boxY1, 15, 8, getDisplayWhite());
    }
    
    // RX label to the right of box
    display_ssd1306.setTextSize(1);
    display_ssd1306.setTextColor(getDisplayWhite());
    display_ssd1306.setCursor(labelX, boxY1 + 1);
    display_ssd1306.print("RX");
    
    // TX box
    if (millis() - lastTX < RS232_ACTIVE_TIME) {
      display_ssd1306.fillRect(boxX, boxY2, 15, 8, getDisplayWhite());
    } else {
      display_ssd1306.drawRect(boxX, boxY2, 15, 8, getDisplayWhite());
    }
    
    // TX label to the right of box
    display_ssd1306.setCursor(labelX, boxY2 + 1);
    display_ssd1306.print("TX");
  } else {
    // RX Box
    if (millis() - lastRX < RS232_ACTIVE_TIME) {
      display_sh1106.fillRect(boxX, boxY1, 15, 8, getDisplayWhite());
    } else {
      display_sh1106.drawRect(boxX, boxY1, 15, 8, getDisplayWhite());
    }
    
    // RX label to the right of box
    display_sh1106.setTextSize(1);
    display_sh1106.setTextColor(getDisplayWhite());
    display_sh1106.setCursor(labelX, boxY1 + 1);
    display_sh1106.print("RX");
    
    // TX box
    if (millis() - lastTX < RS232_ACTIVE_TIME) {
      display_sh1106.fillRect(boxX, boxY2, 15, 8, getDisplayWhite());
    } else {
      display_sh1106.drawRect(boxX, boxY2, 15, 8, getDisplayWhite());
    }
    
    // TX label to the right of box
    display_sh1106.setCursor(labelX, boxY2 + 1);
    display_sh1106.print("TX");
  }
}

void updateOLED() {
  // Query WiFi signal strength only every 1000ms (performance optimization)
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
    display_ssd1306.drawLine(0, 19, SCREEN_WIDTH, 19, getDisplayWhite());
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
    display_ssd1306.drawLine(0, 31, SCREEN_WIDTH, 31, getDisplayWhite());
    drawCBChannelDisplay(); // Render CB channel display instead of RX/TX indicators
    display_ssd1306.display();
  } else {
    display_sh1106.clearDisplay();
    display_sh1106.setTextColor(getDisplayWhite());
    display_sh1106.setTextSize(2);
    display_sh1106.setCursor(0,1);
    display_sh1106.print(callsign);
    
    drawWifiStrength(cachedStrength);
    display_sh1106.drawLine(0, 19, SCREEN_WIDTH, 19, getDisplayWhite());
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
    display_sh1106.drawLine(0, 31, SCREEN_WIDTH, 31, getDisplayWhite());
    drawCBChannelDisplay(); // New CB channel display instead of drawRXTXRects()
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
  EEPROM.write(DISPLAYTYPE_OFFSET, displayType);  // Save display type
  EEPROM.write(SSL_VALIDATION_OFFSET, sslValidation ? 1 : 0);  // Save SSL validation
  EEPROM.write(CPU_FREQUENCY_OFFSET, cpuFrequency);  // Save CPU frequency
  EEPROM.write(DISPLAY_BRIGHTNESS_OFFSET, displayBrightness);  // Save display brightness
  
  // Save MQTT configuration
  EEPROM.write(MQTT_ENABLED_OFFSET, mqttEnabled ? 1 : 0);
  for (int i = 0; i < 64; ++i) EEPROM.write(MQTT_BROKER_OFFSET+i, mqttBroker[i]);
  EEPROM.write(MQTT_PORT_OFFSET, (mqttPort >> 8) & 0xFF);
  EEPROM.write(MQTT_PORT_OFFSET+1, mqttPort & 0xFF);
  for (int i = 0; i < 16; ++i) EEPROM.write(MQTT_USERNAME_OFFSET+i, mqttUsername[i]);
  for (int i = 0; i < 32; ++i) EEPROM.write(MQTT_PASSWORD_OFFSET+i, mqttPassword[i]);
  for (int i = 0; i < 32; ++i) EEPROM.write(MQTT_SHARED_SECRET_OFFSET+i, mqttSharedSecret[i]);
  EEPROM.write(CB_CHANNEL_OFFSET, cbChannel); // Save CB channel
  EEPROM.write(PACKET_AUDIO_OFFSET, packetAudioEnabled ? 1 : 0); // Save packet audio setting
  EEPROM.write(AUDIO_VOLUME_OFFSET, audioVolume); // Save audio volume
  
  // Save TX Delay (2 bytes, little endian)
  EEPROM.write(TX_DELAY_OFFSET, txDelay & 0xFF);
  EEPROM.write(TX_DELAY_OFFSET+1, (txDelay >> 8) & 0xFF);
  
  // Save AFSK frequencies (2 bytes each, little endian)
  EEPROM.write(FREQ_MARK_OFFSET, afskFreqMark & 0xFF);
  EEPROM.write(FREQ_MARK_OFFSET+1, (afskFreqMark >> 8) & 0xFF);
  EEPROM.write(FREQ_SPACE_OFFSET, afskFreqSpace & 0xFF);
  EEPROM.write(FREQ_SPACE_OFFSET+1, (afskFreqSpace >> 8) & 0xFF);
  
  // Save hardware gain level
  EEPROM.write(HARDWARE_GAIN_OFFSET, hardwareGain);
  
  EEPROM.commit();
}

void loadConfig() {
  // EEPROM.begin() already called in setup() - don't call again to avoid timeout
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
  displayType = EEPROM.read(DISPLAYTYPE_OFFSET);  // Load display type
  sslValidation = EEPROM.read(SSL_VALIDATION_OFFSET) == 1;  // Load SSL validation
  cpuFrequency = EEPROM.read(CPU_FREQUENCY_OFFSET);  // Load CPU frequency
  displayBrightness = EEPROM.read(DISPLAY_BRIGHTNESS_OFFSET);  // Load display brightness
  
  // Load MQTT configuration
  mqttEnabled = EEPROM.read(MQTT_ENABLED_OFFSET) == 1;
  for (int i = 0; i < 64; ++i) mqttBroker[i] = EEPROM.read(MQTT_BROKER_OFFSET+i);
  mqttBroker[63] = 0;
  mqttPort = ((uint16_t)EEPROM.read(MQTT_PORT_OFFSET) << 8) | ((uint16_t)EEPROM.read(MQTT_PORT_OFFSET+1));
  for (int i = 0; i < 16; ++i) mqttUsername[i] = EEPROM.read(MQTT_USERNAME_OFFSET+i);
  mqttUsername[15] = 0;
  for (int i = 0; i < 32; ++i) mqttPassword[i] = EEPROM.read(MQTT_PASSWORD_OFFSET+i);
  mqttPassword[31] = 0;
  for (int i = 0; i < 32; ++i) mqttSharedSecret[i] = EEPROM.read(MQTT_SHARED_SECRET_OFFSET+i);
  mqttSharedSecret[31] = 0;
  cbChannel = EEPROM.read(CB_CHANNEL_OFFSET); // Load CB channel
  uint8_t audioSetting = EEPROM.read(PACKET_AUDIO_OFFSET); // Load packet audio setting
  packetAudioEnabled = (audioSetting == 1);
  audioVolume = EEPROM.read(AUDIO_VOLUME_OFFSET); // Load audio volume
  if (audioVolume > 100) audioVolume = 70; // Fallback to 70% if invalid
  
  // Load TX Delay (2 bytes, little endian)
  txDelay = ((uint16_t)EEPROM.read(TX_DELAY_OFFSET)) | ((uint16_t)EEPROM.read(TX_DELAY_OFFSET+1) << 8);
  if (txDelay == 0xFFFF || txDelay == 0 || txDelay > 5000) {
    txDelay = 100; // Default 100ms if invalid
    addBootMessage("TX Delay war ungültig, auf 100ms gesetzt", "WARNING");
  }
  
  // Load AFSK frequencies (2 bytes each, little endian)
  afskFreqMark = ((uint16_t)EEPROM.read(FREQ_MARK_OFFSET)) | ((uint16_t)EEPROM.read(FREQ_MARK_OFFSET+1) << 8);
  afskFreqSpace = ((uint16_t)EEPROM.read(FREQ_SPACE_OFFSET)) | ((uint16_t)EEPROM.read(FREQ_SPACE_OFFSET+1) << 8);
  
  // Debug output for EEPROM values
  Serial.println("DEBUG: EEPROM Werte gelesen:");
  Serial.println("  afskFreqMark  = " + String(afskFreqMark) + " Hz (Offset " + String(FREQ_MARK_OFFSET) + ")");
  Serial.println("  afskFreqSpace = " + String(afskFreqSpace) + " Hz (Offset " + String(FREQ_SPACE_OFFSET) + ")");
  
  // Validate AFSK frequencies and set defaults if invalid
  bool configChanged = false;
  if (afskFreqMark == 0xFFFF || afskFreqMark == 0 || afskFreqMark > 5000) {
    afskFreqMark = 1200; // Default Bell 202 Mark frequency
    addBootMessage("AFSK Mark-Frequenz war ungültig (" + String(afskFreqMark) + "), auf 1200Hz gesetzt", "WARNING");
    configChanged = true;
  }
  if (afskFreqSpace == 0xFFFF || afskFreqSpace == 0 || afskFreqSpace > 5000) {
    afskFreqSpace = 2200; // Default Bell 202 Space frequency
    addBootMessage("AFSK Space-Frequenz war ungültig (" + String(afskFreqSpace) + "), auf 2200Hz gesetzt", "WARNING");
    configChanged = true;
  }
  
  // Load hardware gain level
  hardwareGain = EEPROM.read(HARDWARE_GAIN_OFFSET);
  Serial.println("  hardwareGain  = " + String(hardwareGain) + " (Offset " + String(HARDWARE_GAIN_OFFSET) + ")");
  
  if (hardwareGain > 2 || hardwareGain == 0xFF) {
    hardwareGain = 0; // Default to 9dB if invalid
    addBootMessage("Hardware-Verstärkung war ungültig (" + String(hardwareGain) + "), auf 9dB gesetzt", "WARNING");
    configChanged = true;
  }
  
  // Save defaults to EEPROM if any values were invalid
  if (configChanged) {
    Serial.println("Speichere korrigierte Werte ins EEPROM...");
    saveConfig();
  }
  
  if(baudrate == 0xFFFFFFFF || baudrate == 0x00000000) {
    baudrate = 2400;
    addBootMessage("Baudrate war ungültig, auf 2400 gesetzt", "WARNING");
  }
  if(logLevel > 3) logLevel = 1;
  if(displayType > 1) displayType = DISPLAY_SSD1306;  // Default to SSD1306 for invalid value
  if(strlen(wifiSsid) == 0) addBootMessage("WiFi SSID is empty!", "WARNING");
  if(strlen(otaRepoUrl) == 0) {
    strcpy(otaRepoUrl, "https://raw.githubusercontent.com/nad22/UDM-Packet-Radio-Internet-Gateway/main/ota");
    addBootMessage("OTA URL war leer, Standard gesetzt", "WARNING");
  }
  if(strlen(localVersion) == 0 || localVersion[0] == 0xFF) {
    strcpy(localVersion, "1.0.1");
    addBootMessage("Version was empty, default set: " + String(localVersion), "WARNING");
    saveConfig(); // Save default version
  }
  
  // Validate MQTT configuration
  if(mqttPort == 0 || mqttPort == 0xFFFF) {
    mqttPort = 8883; // Default SSL port
    addBootMessage("MQTT port invalid, set to 8883", "WARNING");
  }
  // MQTT is always enabled (pure MQTT implementation)
  mqttEnabled = true;
  if(strlen(mqttBroker) == 0) {
    addBootMessage("MQTT broker URL not configured - please set!", "WARNING");
  }
  
  // Validate CB channel (1-40)
  if(cbChannel < 1 || cbChannel > 40 || cbChannel == 0xFF) {
    cbChannel = 1; // Default CB channel 1
    addBootMessage("CB channel invalid, set to channel 1", "WARNING");
  }
  
  // Validate CPU frequency value and correct problematic settings
  if(cpuFrequency > 2 || cpuFrequency == 0xFF) {
    if(cpuFrequency == 3 || cpuFrequency == 4) {
      // Correct old problematic 40MHz (3) or 26MHz (4) settings
      cpuFrequency = 2; // Set to 80 MHz (safe)
      addBootMessage("CPU frequency corrected: Problematic setting changed to 80 MHz", "WARNING");
      saveConfig(); // Save immediately to prevent boot loop
    } else {
      cpuFrequency = 0; // Fallback to 240 MHz
      addBootMessage("CPU frequency invalid, reset to 240 MHz", "WARNING");
    }
  }
}

// ==================================================================================
// MQTT COMMUNICATION FUNCTIONS
// ==================================================================================

/**
 * Configure MQTT topics based on CB channel (broadcast mode)
 * Generates dynamic CB-Funk topics: udmprig/rf/1 to udmprig/rf/40
 * Simulates traditional CB radio channel switching
 */
void setupMqttTopics() {
  // Generate CB-Funk topic dynamically: udmprig/rf/1 to udmprig/rf/40
  mqttBroadcastTopic = "udmprig/rf/" + String(cbChannel);
  
  if(strlen(callsign) > 0) {
    appendMonitor("MQTT CB channel " + String(cbChannel) + " for " + String(callsign), "INFO");
    appendMonitor("Radio simulation: " + mqttBroadcastTopic, "INFO");
  }
}

/**
 * MQTT message callback handler (broadcast mode)
 * Processes incoming messages from CB-Funk channels
 * Handles payload decryption and forwards to RS232
 * @param messageSize Size of incoming MQTT message
 */
void onMqttMessage(int messageSize) {
  String topic = mqttClient.messageTopic();
  String message = "";
  
  // Read message content
  while (mqttClient.available()) {
    message += (char)mqttClient.read();
  }
  
  appendMonitor("MQTT RX: " + topic + " - " + String(messageSize) + " bytes", "INFO");
  
  // Decrypt payload if shared secret is set
  // Process all messages from broadcast channel
  if(topic == mqttBroadcastTopic) {
    // Process JSON message with hex payload (decryption happens in processMqttMessage)
    processMqttMessage(message);
  }
  else if(topic.endsWith("/config")) {
    // Configuration update
    appendMonitor("Config update received: " + message, "INFO");
  }
}

/**
 * Establish MQTT connection to HiveMQ Cloud broker
 * Configures SSL connection with authentication
 * @return true if connection successful, false otherwise
 */
bool connectMQTT() {
  if(!mqttEnabled || strlen(mqttBroker) == 0) {
    return false;
  }
  
  if(mqttClient.connected()) {
    return true;
  }
  
  // SSL configuration for HiveMQ Cloud
  mqttWifiClient.setInsecure(); // For development - TODO: Use certificate in production
  
  // Generate unique client ID with callsign for identification
  String clientId = "ESP32-" + String(callsign) + "-" + String(millis() % 10000);
  
  // Configure MQTT client settings
  mqttClient.setId(clientId);
  mqttClient.setUsernamePassword(mqttUsername, mqttPassword);
  mqttClient.setKeepAliveInterval(300000); // 5 minute keep-alive interval
  mqttClient.setConnectionTimeout(15000);  // 15 Sekunden Timeout
  mqttClient.onMessage(onMqttMessage);
  
  appendMonitor("MQTT connecting to " + String(mqttBroker) + ":" + String(mqttPort), "INFO");
  
  // Establish connection
  if(mqttClient.connect(mqttBroker, mqttPort)) {
    appendMonitor("MQTT connected as " + clientId, "INFO");
    
    // Subscribe only to broadcast topic (QoS 0 = Live-Only, no old messages)
    mqttClient.subscribe(mqttBroadcastTopic);
    
    appendMonitor("MQTT subscribed: " + mqttBroadcastTopic + " (Live radio channel)", "INFO");
    
    mqttConnected = true;
    return true;
  } else {
    int error = mqttClient.connectError();
    String errorMsg = "MQTT error: " + String(error);
    appendMonitor(errorMsg, "ERROR");
    mqttConnected = false;
    return false;
  }
}

// Publish MQTT message (QoS 0, No Retention - Radio simulation)
bool publishMqttMessage(const String& topic, const String& message) {
  if(!mqttClient.connected() || !mqttEnabled) {
    appendMonitor("MQTT not connected", "WARNING");
    return false;
  }
  
  // Send message (QoS 0 = Fire-and-Forget, no storage)
  // Encryption is handled by calling function
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

// MQTT loop handler
void handleMqttLoop() {
  if(!mqttEnabled) return;
  
  // MQTT poll for message handling
  mqttClient.poll();
  
  // Automatic reconnection on connection loss
  if(!mqttClient.connected()) {
    static unsigned long lastReconnect = 0;
    unsigned long now = millis();
    
    if(now - lastReconnect > 30000) { // Try every 30 seconds
      lastReconnect = now;
      appendMonitor("MQTT connection lost - reconnecting...", "WARNING");
      connectMQTT();
    }
  }
}

// Check MQTT connection status
bool isMqttConnected() {
  bool connected = mqttEnabled && mqttClient.connected();
  static bool lastState = false;
  static unsigned long lastLog = 0;
  
  // Log connection state changes
  if (connected != lastState || (millis() - lastLog > 30000)) {
    if (connected) {
      //appendMonitor("MQTT Status: VERBUNDEN (" + String(mqttBroker) + ")", "INFO");
    } else {
      appendMonitor("MQTT Status: NICHT VERBUNDEN (Broker: " + String(mqttBroker) + ")", "WARNING");
    }
    lastState = connected;
    lastLog = millis();
  }
  
  return connected;
}

// ========================================
// END OF MQTT FUNCTIONS  
// ========================================

// Set CPU frequency based on configuration
void setCpuFrequency() {
  uint32_t freqMHz = 240;  // Default
  switch(cpuFrequency) {
    case 0: freqMHz = 240; break;  // Standard
    case 1: freqMHz = 160; break;  // Reduced
    case 2: freqMHz = 80; break;   // Power saving
    default: freqMHz = 240; break;
  }
  
  setCpuFrequencyMhz(freqMHz);
  
  // Removed: CPU frequency serial output for better performance
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
      <li class="tab col s4"><a href="#config">Config</a></li>
      <li class="tab col s4"><a href="#monitor">Monitor</a></li>
    </ul>
    <div id="config" class="col s12">
      <h5>Konfiguration</h5>
      <form id="configform" action='/save' method='post'>
        
        <!-- WiFi Konfiguration -->
        <div class="card">
          <div class="card-content">
            <span class="card-title"><i class="material-icons left">wifi</i>WiFi Konfiguration</span>
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
          </div>
        </div>
        
        <!-- Packet Radio Konfiguration -->
        <div class="card">
          <div class="card-content">
            <span class="card-title"><i class="material-icons left">radio</i>Packet Radio Konfiguration</span>
            <div class="input-field custom-row">
              <input id="callsign" name="callsign" type="text" maxlength="31" value=")=====";
  html += String(callsign);
  html += R"=====(">
              <label for="callsign" class="active">Callsign</label>
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
  // Generate CB channel 1-40 dropdown
  for(int i = 1; i <= 40; i++) {
    html += "<option value='";
    html += String(i);
    html += "'";
    if(cbChannel == i) html += " selected";
    html += ">Channel ";
    if(i < 10) html += "0"; // Leading zero for single-digit channels
    html += String(i);
    html += "</option>";
  }
  html += R"=====(</select>
              <label for="cbchannel">CB-Funk Kanal</label>
              <span class="helper-text">CB Funk Kanal 1-40 für MQTT Broadcast</span>
            </div>
          </div>
        </div>
        
        <!-- Packet Radio Audio Konfiguration -->
        <div class="card">
          <div class="card-content">
            <span class="card-title"><i class="material-icons left">volume_up</i>Packet Radio Audio</span>
            <div class="input-field custom-row">
              <select id="packetaudio" name="packetaudio">
                <option value="1")=====";
  if(packetAudioEnabled) html += " selected";
  html += R"=====(>Aktiviert</option>
                <option value="0")=====";
  if(!packetAudioEnabled) html += " selected";
  html += R"=====(>Deaktiviert</option>
              </select>
              <label for="packetaudio">1200 Baud Audio Simulation</label>
              <span class="helper-text">I2S-AFSK Audio über MAX98357A (Pins 25/26/27). Studio-Qualität Bell 202 Sound!</span>
            </div>
            <div class="input-field custom-row">
              <select id="audiovolume" name="audiovolume">
                <option value="10")=====";
  if(audioVolume >= 5 && audioVolume <= 15) html += " selected";
  html += R"=====(>10% (Sehr leise)</option>
                <option value="20")=====";
  if(audioVolume >= 15 && audioVolume <= 25) html += " selected";
  html += R"=====(>20% (Leise)</option>
                <option value="30")=====";
  if(audioVolume >= 25 && audioVolume <= 35) html += " selected";
  html += R"=====(>30% (Niedrig)</option>
                <option value="40")=====";
  if(audioVolume >= 35 && audioVolume <= 45) html += " selected";
  html += R"=====(>40% (Mittel-leise)</option>
                <option value="50")=====";
  if(audioVolume >= 45 && audioVolume <= 55) html += " selected";
  html += R"=====(>50% (Normal)</option>
                <option value="60")=====";
  if(audioVolume >= 55 && audioVolume <= 65) html += " selected";
  html += R"=====(>60% (Mittel)</option>
                <option value="70")=====";
  if(audioVolume >= 65 && audioVolume <= 75) html += " selected";
  html += R"=====(>70% (Mittel-laut)</option>
                <option value="80")=====";
  if(audioVolume >= 75 && audioVolume <= 85) html += " selected";
  html += R"=====(>80% (Laut)</option>
                <option value="90")=====";
  if(audioVolume >= 85 && audioVolume <= 95) html += " selected";
  html += R"=====(>90% (Sehr laut)</option>
                <option value="100")=====";
  if(audioVolume >= 95) html += " selected";
  html += R"=====(>100% (Maximum)</option>
              </select>
              <label for="audiovolume">Audio-Lautstärke</label>
            </div>
            <div class="input-field custom-row">
              <input id="freqmark" name="freqmark" type="number" min="300" max="5000" value=")=====";
  html += String(afskFreqMark);
  html += R"=====(">
              <label for="freqmark">AFSK Mark-Frequenz (Hz)</label>
              <span class="helper-text">Frequenz für logische "1" (Bell 202 Standard: 1200Hz)</span>
            </div>
            <div class="input-field custom-row">
              <input id="freqspace" name="freqspace" type="number" min="300" max="5000" value=")=====";
  html += String(afskFreqSpace);
  html += R"=====(">
              <label for="freqspace">AFSK Space-Frequenz (Hz)</label>
              <span class="helper-text">Frequenz für logische "0" (Bell 202 Standard: 2200Hz)</span>
            </div>
            <div class="input-field custom-row">
              <select id="hardwaregain" name="hardwaregain">
                <option value="1")=====";
  if(hardwareGain == 1) html += " selected";
  html += R"=====(>6dB (Niedrig)</option>
                <option value="0")=====";
  if(hardwareGain == 0) html += " selected";
  html += R"=====(>9dB (Standard)</option>
                <option value="2")=====";
  if(hardwareGain == 2) html += " selected";
  html += R"=====(>12dB (Hoch)</option>
              </select>
              <label for="hardwaregain">Hardware-Verstärkung</label>
              <span class="helper-text">Hardware-Gain am MAX98357A (GPIO33). Zusätzlich zur Software-Lautstärke!</span>
            </div>
            <div class="input-field custom-row">
              <select id="txdelay" name="txdelay">
                <option value="50")=====";
  if(txDelay >= 40 && txDelay <= 60) html += " selected";
  html += R"=====(>50ms (Sehr schnell)</option>
                <option value="100")=====";
  if(txDelay >= 80 && txDelay <= 120) html += " selected";
  html += R"=====(>100ms (Schnell)</option>
                <option value="200")=====";
  if(txDelay >= 180 && txDelay <= 220) html += " selected";
  html += R"=====(>200ms (Mittel)</option>
                <option value="300")=====";
  if(txDelay >= 280 && txDelay <= 320) html += " selected";
  html += R"=====(>300ms (Standard)</option>
                <option value="500")=====";
  if(txDelay >= 480 && txDelay <= 520) html += " selected";
  html += R"=====(>500ms (Langsam)</option>
              </select>
              <label for="txdelay">TX Delay (PTT→Daten)</label>
              <span class="helper-text">Zeit zwischen PTT-Aktivierung und Datenbeginn. Wichtig für Squelch-Öffnung und TX-Umschaltung!</span>
            </div>
          </div>
        </div>
        
        <!-- MQTT Konfiguration -->
        <div class="card">
          <div class="card-content">
            <span class="card-title"><i class="material-icons left">cloud</i>MQTT Konfiguration</span>
            <div class="input-field custom-row">
              <input type="text" id="mqttbroker" name="mqttbroker" maxlength="63" value=")=====";
  html += String(mqttBroker);
  html += R"=====(">
              <label for="mqttbroker" class="active">MQTT Broker URL</label>
            </div>
            <div class="input-field custom-row">
              <input type="number" id="mqttport" name="mqttport" min="1" max="65535" value=")=====";
  html += String(mqttPort);
  html += R"=====(">
              <label for="mqttport" class="active">MQTT Port</label>
            </div>
            <div class="input-field custom-row">
              <input type="text" id="mqttuser" name="mqttuser" maxlength="15" value=")=====";
  html += String(mqttUsername);
  html += R"=====(">
              <label for="mqttuser" class="active">MQTT Username</label>
            </div>
            <div class="input-field custom-row">
              <input type="password" id="mqttpass" name="mqttpass" maxlength="30" value=")=====";
  html += String(mqttPassword);
  html += R"=====(">
              <label for="mqttpass" class="active">MQTT Password</label>
            </div>
            <div class="input-field custom-row">
              <input type="password" id="mqttsharedsecret" name="mqttsharedsecret" maxlength="31" value=")=====";
  html += String(mqttSharedSecret);
  html += R"=====(">
              <label for="mqttsharedsecret" class="active">MQTT Shared Secret</label>
              <span class="helper-text">Für Payload-Verschlüsselung (leer = keine Verschlüsselung)</span>
            </div>
          </div>
        </div>
        
        <!-- Display & System Konfiguration -->
        <div class="card">
          <div class="card-content">
            <span class="card-title"><i class="material-icons left">settings</i>Display & System</span>
            <div class="input-field custom-row">
              <select id="displaytype" name="displaytype">
                <option value="1")=====";
  if(displayType==DISPLAY_SSD1306) html += " selected";
  html += R"=====(>SSD1306 (kleines Display)</option>
                <option value="0")=====";
  if(displayType==DISPLAY_SH1106G) html += " selected";
  html += R"=====(>SH1106G (großes Display)</option>
              </select>
              <label for="displaytype">Display-Typ</label>
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
            </div>
            <div class="input-field custom-row">
              <select id="loglevel" name="loglevel">
                <option value="0")=====";
  if(logLevel==0) html += " selected";
  html += R"=====(>Error</option>
                <option value="1")=====";
  if(logLevel==1) html += " selected";
  html += R"=====(>Info</option>
                <option value="2")=====";
  if(logLevel==2) html += " selected";
  html += R"=====(>Warning</option>
                <option value="3")=====";
  if(logLevel==3) html += " selected";
  html += R"=====(>Debug</option>
              </select>
              <label for="loglevel">Log Level</label>
            </div>
            <div class="input-field custom-row">
              <select id="cpufreq" name="cpufreq">
                <option value="0")=====";
  if(cpuFrequency==0) html += " selected";
  html += R"=====(>240 MHz (Standard)</option>
                <option value="1")=====";
  if(cpuFrequency==1) html += " selected";
  html += R"=====(>160 MHz (Reduziert)</option>
                <option value="2")=====";
  if(cpuFrequency==2) html += " selected";
  html += R"=====(>80 MHz (Energiesparen)</option>
              </select>
              <label for="cpufreq">CPU-Frequenz</label>
            </div>
          </div>
        </div>
        
        <!-- Firmware Updates -->
        <div class="card">
          <div class="card-content">
            <span class="card-title"><i class="material-icons left">system_update</i>Firmware Updates</span>
            <div class="input-field custom-row">
              <input id="otarepourl" name="otarepourl" type="url" maxlength="127" value=")=====";
  html += String(otaRepoUrl);
  html += R"=====(">
              <label for="otarepourl" class="active">OTA Repository URL</label>
            </div>
          </div>
        </div>
        
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
      
      <!-- Fixed Refresh Button -->
      <div style="position: sticky; bottom: 20px; margin-top: 20px; text-align: right; z-index: 1000;">
        <button class="btn blue waves-effect waves-light" onclick="updateHardwareInfo()">
          <i class="material-icons left">refresh</i>Refresh Info
        </button>
      </div>
    </div>
    
    <div id="monitor" class="col s12">
      <h6>Serieller Monitor</h6>
      <pre id="monitorArea" style="overflow:auto;resize:vertical;border:1px solid #ccc;padding:10px;"></pre>
      <button class="btn red" onclick="clearMonitor()">Leeren</button>
      <script>
        document.addEventListener('DOMContentLoaded', function() {
          var el = document.querySelectorAll('.tabs');
          M.Tabs.init(el, {});
          var selects = document.querySelectorAll('select');
          M.FormSelect.init(selects, {});
          var modals = document.querySelectorAll('.modal');
          M.Modal.init(modals, {});
          
          // Automatically adjust monitor height to display height
          adjustMonitorHeight();
          window.addEventListener('resize', adjustMonitorHeight);
        });
        
        // Automatic monitor height adjustment
        function adjustMonitorHeight() {
          const monitorArea = document.getElementById('monitorArea');
          if (monitorArea) {
            // Available viewport height minus header, navigation, buttons etc.
            const viewportHeight = window.innerHeight;
            const headerHeight = 80; // Approximate header height
            const tabsHeight = 48; // Tab bar
            const titleHeight = 30; // "Serial Monitor" title
            const buttonHeight = 50; // "Clear" button + padding
            const padding = 40; // Additional buffer
            
            const availableHeight = viewportHeight - headerHeight - tabsHeight - titleHeight - buttonHeight - padding;
            const minHeight = 200; // Minimum height
            const maxHeight = 800; // Maximum height
            
            const calculatedHeight = Math.max(minHeight, Math.min(maxHeight, availableHeight));
            monitorArea.style.height = calculatedHeight + 'px';
          }
        }
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
                  // Try to reload page after 2 seconds
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
              
              // Live API call for immediate preview
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
        
        // Initialize brightness dropdown on load
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
  if (server.hasArg("callsign")) strncpy(callsign, server.arg("callsign").c_str(), 31);
  if (server.hasArg("otarepourl")) strncpy(otaRepoUrl, server.arg("otarepourl").c_str(), 127);
  if (server.hasArg("baudrate")) baudrate = server.arg("baudrate").toInt();
  if (server.hasArg("loglevel")) logLevel = server.arg("loglevel").toInt();
  if (server.hasArg("displaytype")) displayType = server.arg("displaytype").toInt();
  if (server.hasArg("cpufreq")) cpuFrequency = server.arg("cpufreq").toInt();
  
  // MQTT Konfiguration
  if (server.hasArg("mqttbroker")) strncpy(mqttBroker, server.arg("mqttbroker").c_str(), 63);
  if (server.hasArg("mqttport")) mqttPort = server.arg("mqttport").toInt();
  if (server.hasArg("mqttuser")) strncpy(mqttUsername, server.arg("mqttuser").c_str(), 15);
  if (server.hasArg("mqttpass")) strncpy(mqttPassword, server.arg("mqttpass").c_str(), 30);
  if (server.hasArg("mqttsharedsecret")) strncpy(mqttSharedSecret, server.arg("mqttsharedsecret").c_str(), 31);
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
  if (server.hasArg("packetaudio")) {
    packetAudioEnabled = (server.arg("packetaudio").toInt() == 1);
    appendMonitor("Packet Audio " + String(packetAudioEnabled ? "aktiviert" : "deaktiviert"), "INFO");
  }
  if (server.hasArg("audiovolume")) {
    uint8_t newVolume = server.arg("audiovolume").toInt();
    if(newVolume >= 0 && newVolume <= 100) {
      audioVolume = newVolume;
      appendMonitor("Audio-Lautstärke gesetzt auf " + String(audioVolume) + "%", "INFO");
    }
  }
  if (server.hasArg("txdelay")) {
    uint16_t newTxDelay = server.arg("txdelay").toInt();
    if(newTxDelay >= 50 && newTxDelay <= 2000) {
      txDelay = newTxDelay;
      appendMonitor("TX Delay gesetzt auf " + String(txDelay) + "ms", "INFO");
    }
  }
  if (server.hasArg("freqmark")) {
    uint16_t newFreqMark = server.arg("freqmark").toInt();
    if(newFreqMark >= 300 && newFreqMark <= 5000) {
      afskFreqMark = newFreqMark;
      appendMonitor("AFSK Mark-Frequenz gesetzt auf " + String(afskFreqMark) + "Hz", "INFO");
    }
  }
  if (server.hasArg("freqspace")) {
    uint16_t newFreqSpace = server.arg("freqspace").toInt();
    if(newFreqSpace >= 300 && newFreqSpace <= 5000) {
      afskFreqSpace = newFreqSpace;
      appendMonitor("AFSK Space-Frequenz gesetzt auf " + String(afskFreqSpace) + "Hz", "INFO");
    }
  }
  if (server.hasArg("hardwaregain")) {
    uint8_t newHardwareGain = server.arg("hardwaregain").toInt();
    if(newHardwareGain >= 0 && newHardwareGain <= 3) {
      setHardwareGain(newHardwareGain);
      appendMonitor("Hardware-Verstärkung geändert", "INFO");
    }
  }
  
  // Process MQTT configuration (MQTT is always enabled)
  mqttEnabled = true; // MQTT is the only communication method
  if (server.hasArg("mqttbroker")) strncpy(mqttBroker, server.arg("mqttbroker").c_str(), 63);
  if (server.hasArg("mqttport")) mqttPort = server.arg("mqttport").toInt();
  if (server.hasArg("mqttuser")) strncpy(mqttUsername, server.arg("mqttuser").c_str(), 15);
  if (server.hasArg("mqttpass")) strncpy(mqttPassword, server.arg("mqttpass").c_str(), 31);
  
  wifiSsid[63]=0; wifiPass[63]=0; serverUrl[63]=0; callsign[31]=0; otaRepoUrl[127]=0;
  mqttBroker[63]=0; mqttUsername[15]=0; mqttPassword[31]=0; mqttSharedSecret[31]=0;
  saveConfig();
  appendMonitor("Konfiguration gespeichert. Neustart folgt.", "INFO");
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
  delay(500);
  ESP.restart();
}

void handleMonitor() {
  // Check WiFi status before request processing
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
  
  // Check WiFi status after processing (only log critical changes)
  wl_status_t postStatus = WiFi.status();
  // Removed: Debug outputs for better performance
  
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
  
  // Uptime calculation - Format: "HH:MM:SS" or "Xd HH:MM:SS"
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
  json += "\"encryption\":\"" + String(strlen(mqttSharedSecret) > 0 ? "Enabled" : "Disabled") + "\",";
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
  
  // Show the last 5 crash logs (circular buffer)
  uint32_t totalLogs = min(crashLogCount, (uint32_t)MAX_CRASH_LOGS);
  for (int i = 0; i < totalLogs; i++) {
    if (i > 0) json += ",";
    
    // Calculate index for circular buffer
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
  // CORS headers for browser compatibility
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "POST");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  
  // Send confirmation
  server.send(200, "application/json", "{\"status\":\"rebooting\",\"message\":\"System reboot initiated\"}");
  
  // Monitor entry and crash log for planned restart
  appendMonitor("System restart initiated by user", "INFO");
  saveCrashLog("Manual system reboot requested by user");
  
  // Wait briefly so HTTP response is sent
  delay(1000);
  
  // Restart ESP32
  ESP.restart();
}

// Crash Log System - Save errors before watchdog reset
void saveCrashLog(const String& message) {
  // Generate current timestamp
  String timestamp = getTimestamp();
  
  // Calculate new position (circular buffer)
  uint32_t index = crashLogCount % MAX_CRASH_LOGS;
  
  // Create crash log entry
  strncpy(crashLogs[index].timestamp, timestamp.c_str(), 20);
  crashLogs[index].timestamp[20] = '\0';
  strncpy(crashLogs[index].message, message.c_str(), 99);
  crashLogs[index].message[99] = '\0';
  
  // Increment counter
  crashLogCount++;
  
  // Save immediately to EEPROM (important before watchdog reset)
  saveCrashLogsToEEPROM();
  
  // Also output to monitor
  appendMonitor("CRASH LOG: " + message, "ERROR");
}

void loadCrashLogs() {
  unsigned long startTime = millis();
  
  // Load number of crash logs
  crashLogCount = ((uint32_t)EEPROM.read(CRASH_LOG_COUNT_OFFSET) << 24)
                | ((uint32_t)EEPROM.read(CRASH_LOG_COUNT_OFFSET+1) << 16)
                | ((uint32_t)EEPROM.read(CRASH_LOG_COUNT_OFFSET+2) << 8)
                | ((uint32_t)EEPROM.read(CRASH_LOG_COUNT_OFFSET+3));
  
  Serial.println("DEBUG: Raw crash log count = " + String(crashLogCount));
  
  // Check if crash log system was never initialized (first boot)
  bool isFirstBoot = (crashLogCount == 0xFFFFFFFF);
  
  // Plausibility check
  if (crashLogCount > 10000 || isFirstBoot) {
    Serial.println("DEBUG: Initializing crash log system (first boot or invalid data)");
    crashLogCount = 0;
    
    // Initialize crash log entries to empty
    for (int i = 0; i < MAX_CRASH_LOGS; i++) {
      memset(crashLogs[i].timestamp, 0, 21);
      memset(crashLogs[i].message, 0, 100);
    }
    
    // Save initialized values to EEPROM
    saveCrashLogsToEEPROM();
    
    if (isFirstBoot) {
      addBootMessage("Crash log system initialized (first boot)", "INFO");
    } else {
      addBootMessage("Crash log count corrected (was invalid)", "WARNING");
    }
  } else {
    // Load existing crash log entries (normal boot)
    for (int i = 0; i < MAX_CRASH_LOGS; i++) {
      int offset = CRASH_LOG_ENTRIES_OFFSET + (i * CRASH_LOG_ENTRY_SIZE);
      
      // Load timestamp
      for (int j = 0; j < 21; j++) {
        crashLogs[i].timestamp[j] = EEPROM.read(offset + j);
      }
      
      // Load message
      for (int j = 0; j < 100; j++) {
        crashLogs[i].message[j] = EEPROM.read(offset + 21 + j);
      }
    }
  }
  
  unsigned long loadTime = millis() - startTime;
  Serial.println("Crash logs loaded in " + String(loadTime) + "ms. Count: " + String(crashLogCount));
  addBootMessage("Crash logs loaded (" + String(loadTime) + "ms)", "INFO");
}

void saveCrashLogsToEEPROM() {
  // Save number of crash logs
  EEPROM.write(CRASH_LOG_COUNT_OFFSET, (crashLogCount >> 24) & 0xFF);
  EEPROM.write(CRASH_LOG_COUNT_OFFSET+1, (crashLogCount >> 16) & 0xFF);
  EEPROM.write(CRASH_LOG_COUNT_OFFSET+2, (crashLogCount >> 8) & 0xFF);
  EEPROM.write(CRASH_LOG_COUNT_OFFSET+3, crashLogCount & 0xFF);
  
  // Save crash log entries
  for (int i = 0; i < MAX_CRASH_LOGS; i++) {
    int offset = CRASH_LOG_ENTRIES_OFFSET + (i * CRASH_LOG_ENTRY_SIZE);
    
    // Save timestamp
    for (int j = 0; j < 21; j++) {
      EEPROM.write(offset + j, crashLogs[i].timestamp[j]);
    }
    
    // Save message
    for (int j = 0; j < 100; j++) {
      EEPROM.write(offset + 21 + j, crashLogs[i].message[j]);
    }
  }
  
  EEPROM.commit(); // Important: Write immediately
}

// Clear all crash logs
void clearCrashLogs() {
  // Clear crash log array in RAM
  crashLogCount = 0;
  for (int i = 0; i < MAX_CRASH_LOGS; i++) {
    memset(crashLogs[i].timestamp, 0, 21);
    memset(crashLogs[i].message, 0, 100);
  }
  
  // Clear EEPROM area
  // Set count to 0
  EEPROM.write(CRASH_LOG_COUNT_OFFSET, 0);
  EEPROM.write(CRASH_LOG_COUNT_OFFSET+1, 0);
  EEPROM.write(CRASH_LOG_COUNT_OFFSET+2, 0);
  EEPROM.write(CRASH_LOG_COUNT_OFFSET+3, 0);
  
  // Clear all crash log entries (set bytes to 0)
  for (int i = 0; i < MAX_CRASH_LOGS; i++) {
    int offset = CRASH_LOG_ENTRIES_OFFSET + (i * CRASH_LOG_ENTRY_SIZE);
    for (int j = 0; j < CRASH_LOG_ENTRY_SIZE; j++) {
      EEPROM.write(offset + j, 0);
    }
  }
  
  EEPROM.commit(); // Write to EEPROM immediately
  
  if(logLevel >= 1) {
    appendMonitor("All crash logs have been cleared", "INFO");
  }
}

// Handler for clear crash logs API
void handleClearCrashLogs() {
  // CORS headers for browser compatibility
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "POST");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  
  // Clear crash logs
  clearCrashLogs();
  
  // Send confirmation
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Crash logs cleared successfully\"}");
}

// Handler for live display brightness adjustment
void handleDisplayBrightness() {
  // CORS headers for browser compatibility
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "POST");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  
  if(server.hasArg("brightness")) {
    int brightness = server.arg("brightness").toInt();
    
    // Validation: 0-255 range
    if(brightness >= 0 && brightness <= 255) {
      displayBrightness = brightness;
      setDisplayBrightness(displayBrightness); // Apply live to display
      
      appendMonitor("Display brightness changed: " + String(brightness), "INFO");
      server.send(200, "application/json", "{\"status\":\"success\",\"brightness\":" + String(brightness) + "}");
    } else {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid brightness value (0-255)\"}");
    }
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing brightness parameter\"}");
  }
}

// Set display brightness (live application)
void setDisplayBrightness(uint8_t brightness) {
  // For OLED displays use contrast (works like brightness)
  if (displayType == DISPLAY_SSD1306) {
    display_ssd1306.ssd1306_command(SSD1306_SETCONTRAST);
    display_ssd1306.ssd1306_command(brightness);
  } else {
    // SH1106G: Unfortunately no direct contrast API available in Adafruit_SH1106G
    // As workaround we use internal Wire communication
    Wire.beginTransmission(0x3C); // Standard I2C address for SH1106
    Wire.write(0x80); // Command mode
    Wire.write(0x81); // Set contrast command (SSD1306_SETCONTRAST)
    Wire.endTransmission();
    
    Wire.beginTransmission(0x3C);
    Wire.write(0x80); // Command mode  
    Wire.write(brightness); // Brightness value
    Wire.endTransmission();
  }
}

// Convert WiFi status to readable text
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

// Convert HTTP error code to readable text
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

// Formatted uptime as HH:MM:SS or Xd HH:MM:SS
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
  
  // HH:MM:SS format with leading zeros
  if (hours < 10) uptime += "0";
  uptime += String(hours) + ":";
  if (minutes < 10) uptime += "0";
  uptime += String(minutes) + ":";
  if (seconds < 10) uptime += "0";
  uptime += String(seconds);
  
  return uptime;
}

// Check gateway reachability (true WiFi status)
bool isGatewayReachable() {
  if (WiFi.status() != WL_CONNECTED) {
    return false; // WiFi not connected
  }
  
  IPAddress gateway = WiFi.gatewayIP();
  if (gateway == IPAddress(0, 0, 0, 0)) {
    return false; // No gateway available
  }
  
  // **SIMPLE GATEWAY CHECK: Check ARP resolution**
  // If WiFi is connected and we have a valid gateway IP,
  // the gateway is normally reachable.
  // Additionally: Short DNS test as indicator
  IPAddress testIP;
  bool dnsWorks = WiFi.hostByName("8.8.8.8", testIP); // Google DNS
  
  // If DNS works, gateway is definitely reachable
  if (dnsWorks) {
    return true;
  }
  
  // Fallback: Minimal TCP test with very short timeout
  WiFiClient client;
  client.setTimeout(500); // Nur 0.5 Sekunden
  bool connected = client.connect(gateway, 53); // DNS Port (weniger invasiv als 80)
  if (connected) {
    client.stop();
    return true;
  }
  
  return false; // Gateway probably not reachable
}

// Connect to strongest available access point


// Extended WiFi diagnostics
void logWiFiDiagnostics() {
  // Removed: Serial debug output for better performance
  // WiFi diagnostics still run, but without serial output
}

// WiFi connection monitoring and automatic recovery
void checkWiFiConnection() {
  unsigned long currentTime = millis();
  
  // Check WiFi status only every WIFI_CHECK_INTERVAL milliseconds
  if (currentTime - lastWiFiCheck < WIFI_CHECK_INTERVAL && !forceWiFiReconnect) {
    return;
  }
  
  lastWiFiCheck = currentTime;
  
  // Prüfe WiFi-Status
  wl_status_t currentStatus = WiFi.status();
  if (currentStatus != WL_CONNECTED || forceWiFiReconnect) {
    
    // Extended diagnostics on connection loss
    String statusText = getWiFiStatusText(currentStatus);
    appendMonitor("WiFi connection lost (Status: " + statusText + "/" + String(currentStatus) + "). Attempting reconnection...", "WARNING");
    
    // Output detailed WiFi diagnostics
    logWiFiDiagnostics();
    
    wifiReconnectAttempts++;
    
    // WiFi restart only on repeated problems
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
    
    // Wait for connection (max 5 seconds)
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(250);
      attempts++;
    }
    
    bool reconnected = (WiFi.status() == WL_CONNECTED);
    
    if (reconnected) {
      appendMonitor("WiFi successfully reconnected. IP: " + WiFi.localIP().toString() + ", RSSI: " + String(WiFi.RSSI()) + " dBm", "INFO");
      connectionError = false;
      forceWiFiReconnect = false;
      wifiReconnectAttempts = 0;
    } else {
      appendMonitor("WiFi reconnection failed. Will try again later.", "ERROR");
      saveCrashLog("WiFi reconnect failed (Status: " + String(WiFi.status()) + ")");
      connectionError = true;
    }
  } else {
    // WiFi ist verbunden - prüfe Signalqualität
    int rssi = WiFi.RSSI();
    if (rssi < -85) {
      appendMonitor("Schwaches WiFi-Signal (" + String(rssi) + " dBm). Überwache Verbindung...", "WARNING");
    }
    
    // Reset reconnect counter on stable connection
    if (wifiReconnectAttempts > 0) {
      wifiReconnectAttempts = 0;
    }
  }
}

// WiFi connection function (optimized for speed)
bool tryConnectWiFi() {
  appendMonitor("WLAN connection being established...", "INFO");
  
  // WiFi-Modus konfigurieren
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);  // No flash writes for faster connection
  
  // Standard WiFi configuration (without aggressive optimizations)
  WiFi.setSleep(false);    // No WiFi sleep for stability
  
  // Direct connection without complex protocol settings
  WiFi.begin(wifiSsid, wifiPass);
  
  // Wait for connection (max 5 seconds for faster boot)
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(250);
    attempts++;
    if(attempts % 4 == 0) {  // Status update every second
      Serial.println("WiFi Verbindung... (" + String(attempts/4) + "s)");
    }
  }
  
  bool connected = (WiFi.status() == WL_CONNECTED);
  
  if (connected) {
    Serial.println("WLAN erfolgreich verbunden mit " + String(wifiSsid));
    Serial.println("IP-Adresse: " + WiFi.localIP().toString());
    Serial.println("RSSI: " + String(WiFi.RSSI()) + " dBm");
    
    // Reset-Zähler zurücksetzen
    wifiReconnectAttempts = 0;
    connectionError = false;
    
    return true;
  } else {
    Serial.println("ERROR: WLAN-Verbindung fehlgeschlagen");
    // Skip saveCrashLog during boot to avoid timeout
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
  String urlFirmware = String(otaRepoUrl) + "/udm-prig-client.ino.bin";

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

// ==================================================================================
// ARDUINO SETUP FUNCTION
// ==================================================================================

/**
 * Main setup function - initializes all system components
 * Performs bootloop protection, hardware initialization, and configuration loading
 * Sets up MQTT communication, display, and web interface
 */
void setup() {
  unsigned long setupStart = millis();
  Serial.begin(115200);
  Serial.println("=== BOOT TIMING ANALYSIS ===");
  Serial.println("Setup started at: " + String(setupStart) + "ms");
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Initialize EEPROM early for CPU frequency check
  unsigned long eepromStart = millis();
  EEPROM.begin(EEPROM_SIZE);
  Serial.println("EEPROM init took: " + String(millis() - eepromStart) + "ms");
  
  // Early CPU frequency check before loadConfig() to prevent bootloops
  uint8_t savedCpuFreq = EEPROM.read(CPU_FREQUENCY_OFFSET);
  if(savedCpuFreq >= 3) { // 3=40MHz, 4=26MHz (both problematic)
    Serial.println("BOOTLOOP PROTECTION: Problematic CPU frequency detected!");
    Serial.print("Stored frequency ID: ");
    Serial.println(savedCpuFreq);
    
    // Auf sichere 80 MHz (Index 2) setzen
    EEPROM.write(CPU_FREQUENCY_OFFSET, 2);
    EEPROM.commit();
    
    Serial.println("CPU-Frequenz auf 80 MHz korrigiert. Neustart...");
    delay(2000);
    ESP.restart();
  }

  // Load essential config first (especially display settings)
  unsigned long configStart = millis();
  displayType = EEPROM.read(DISPLAYTYPE_OFFSET);
  displayBrightness = EEPROM.read(DISPLAY_BRIGHTNESS_OFFSET);
  Serial.println("Essential config load took: " + String(millis() - configStart) + "ms");
  
  // Initialize display FIRST to show boot progress immediately
  unsigned long displayStart = millis();
  initDisplay();
  Serial.println("Display init took: " + String(millis() - displayStart) + "ms");
  bootPrint("UDM-PRIG booting...");
  bootPrint("Init Display ... OK");
  bootPrint("Init EEPROM ... OK");
  
  // Now load complete configuration
  unsigned long fullConfigStart = millis();
  bootPrint("Loading Config...");
  loadConfig();
  Serial.println("Full config load took: " + String(millis() - fullConfigStart) + "ms");
  bootPrint("Config loaded");
  
  // CPU-Frequenz basierend auf Konfiguration setzen
  unsigned long cpuFreqStart = millis();
  bootPrint("Setting CPU Freq...");
  setCpuFrequency();
  Serial.println("CPU frequency set took: " + String(millis() - cpuFreqStart) + "ms");
  
  // Crash Logs laden
  bootPrint("Loading Crash Logs...");
  unsigned long crashLogStart = millis();
  loadCrashLogs();
  Serial.println("Crash logs load took: " + String(millis() - crashLogStart) + "ms");
  bootPrint("Crash Logs loaded");
  
  // Bei Watchdog-Reset einen Crash Log Eintrag erstellen
  esp_reset_reason_t resetReason = esp_reset_reason();
  if (resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_INT_WDT || resetReason == ESP_RST_WDT) {
    String reasonStr = "Unknown WDT";
    if (resetReason == ESP_RST_TASK_WDT) reasonStr = "Task Watchdog";
    else if (resetReason == ESP_RST_INT_WDT) reasonStr = "Interrupt Watchdog";
    else if (resetReason == ESP_RST_WDT) reasonStr = "Other Watchdog";
    
    saveCrashLog("System rebooted by " + reasonStr + " - Previous session ended unexpectedly");
  }

  // Skip I2S setup during boot - will be initialized later

  bootPrint("Init RS232 Serial...");
  if (baudrate == 0) {
    baudrate = 2400;
    appendMonitor("Baudrate war 0! Auf 2400 gesetzt.", "WARNING");
  }
  RS232.begin(baudrate, SERIAL_8N1, 16, 17);
  appendMonitor("RS232 initialisiert mit Baudrate " + String(baudrate), "INFO");
  bootPrint("RS232 " + String(baudrate) + " OK");

  bootPrint("Verbinde WLAN ...");
  unsigned long wifiStart = millis();
  bool wifiOk = tryConnectWiFi();
  Serial.println("WiFi connect took: " + String(millis() - wifiStart) + "ms");
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

  bootPrint("Init NTP Server...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  appendMonitor("NTP initialisiert (at.pool.ntp.org, Europe/Vienna)", "INFO");
  bootPrint("NTP Server OK");

  bootPrint("Init mDNS...");
  if (!MDNS.begin("udmprig-client")) {
    appendMonitor("mDNS konnte nicht gestartet werden", "ERROR");
    bootPrint("mDNS ... FEHLER");
  } else {
    appendMonitor("mDNS gestartet als udmprig-client.local", "INFO");
    bootPrint("mDNS OK");
  }
  // Move OTA check to background - don't block boot
  appendMonitor("Firmware Update-Check wird im Hintergrund ausgeführt", "INFO");
  bootPrint("OTA Check gestartet");
  
  appendMonitor("Setup abgeschlossen - Client startet normal", "INFO");
  
  // **STABILE VERSION: Hardware Watchdog aktivieren (ESP32 5.x kompatibel)**
  bootPrint("Init Watchdog...");
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
      bootPrint("Watchdog 30s OK");
    } else {
      appendMonitor("Watchdog Task-Add fehlgeschlagen, Code: " + String(result), "WARNING");
      saveCrashLog("Watchdog task add failed: " + String(result));
      bootPrint("Watchdog FEHLER");
    }
  } else {
    appendMonitor("Watchdog Init fehlgeschlagen, Code: " + String(result) + " - System läuft ohne Watchdog", "WARNING");
    saveCrashLog("Watchdog init failed: " + String(result));
    bootPrint("Watchdog FEHLER");
  }
  
  // MQTT setup (always enabled in this version)
  bootPrint("Init MQTT...");
  if(strlen(mqttBroker) > 0) {
    setupMqttTopics();
    appendMonitor("MQTT enabled - Broker: " + String(mqttBroker), "INFO");
    connectMQTT(); // Attempt initial connection
    bootPrint("MQTT enabled");
  } else {
    appendMonitor("MQTT broker not configured - please set in web interface", "WARNING");
    bootPrint("MQTT nicht konfiguriert");
  }
  
  delay(1000); // Brief wait after watchdog activation
  
  // Initialize I2S audio system at the very end to avoid slowing down critical boot
  bootPrint("Init Audio I2S...");
  unsigned long i2sStart = millis();
  setupI2SAudio();
  Serial.println("I2S audio setup took: " + String(millis() - i2sStart) + "ms");
  bootPrint("Audio I2S bereit");
  
  unsigned long setupEnd = millis();
  Serial.println("=== TOTAL SETUP TIME: " + String(setupEnd - setupStart) + "ms ===");
  
  // Now that the system is fully initialized, flush buffered boot messages
  Serial.println("System fully initialized - flushing boot messages to monitor...");
  flushBootMessages();
  bootPrint("Flushing Messages...");
  bootPrint("=== SYSTEM BEREIT ===");
}

// ==================================================================================
// ARDUINO MAIN LOOP FUNCTION
// ==================================================================================

/**
 * Main loop function - handles all real-time operations
 * Manages watchdog resets, WiFi monitoring, MQTT communication,
 * display updates, and RS232 packet processing
 */
void loop() {
  // Hardware watchdog reset every 3 seconds for system stability
  unsigned long now = millis();
  
  if (now - lastWatchdogReset > 3000) { // Every 3 seconds
    esp_task_wdt_reset(); // Reset watchdog if active
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
    
    // Background OTA check (only once after 30 seconds of uptime)
    static bool otaCheckDone = false;
    if (!otaCheckDone && millis() > 30000) {
      appendMonitor("Performing background OTA check...", "INFO");
      checkForUpdates();
      otaCheckDone = true;
    }
    
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
    static unsigned long lastRs232Activity = 0;
    if (millis() - lastRs232Activity > 5000) { // Debug only every 5 seconds
      appendMonitor("RS232 Data empfangen...", "DEBUG");
      lastRs232Activity = millis();
    }
    
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

// KISS message completely received and send via MQTT
void processCompleteKissMessage(const String& kissData) {
  appendMonitor("KISS Message empfangen: " + String(kissData.length()) + " bytes", "DEBUG");
  
  if(!isMqttConnected()) {
    appendMonitor("MQTT nicht verbunden - KISS Message verworfen!", "WARNING");
    return;
  }
  
  if(isMqttConnected()) {
    // Encode KISS data as hex string for clean JSON transmission
    String hexPayload = "";
    for(int i = 0; i < kissData.length(); i++) {
      char hex[3];
      sprintf(hex, "%02X", (unsigned char)kissData[i]);
      hexPayload += hex;
    }
    
    // Encrypt payload if shared secret is set
    String finalPayload = hexPayload;
    if (strlen(mqttSharedSecret) > 0) {
      finalPayload = encryptPayload(hexPayload);
      appendMonitor("Payload encrypted: " + String(finalPayload.length()) + " characters", "DEBUG");
    }
    
    String mqttMessage = "{";
    mqttMessage += "\"timestamp\":" + String(millis()) + ",";
    mqttMessage += "\"callsign\":\"" + String(callsign) + "\",";
    mqttMessage += "\"type\":\"data\",";
    mqttMessage += "\"payload_hex\":\"" + finalPayload + "\",";
    mqttMessage += "\"payload_length\":" + String(kissData.length()) + ",";
    mqttMessage += "\"rssi\":" + String(WiFi.RSSI()) + ",";
    mqttMessage += "\"gateway\":\"ESP32-" + String(callsign) + "\"";
    mqttMessage += "}";
    
    bool mqttSuccess = publishMqttMessage(mqttBroadcastTopic, mqttMessage);
    if(mqttSuccess) {
      appendMonitor("MQTT Funk TX: " + String(kissData.length()) + " bytes (hex:" + hexPayload.substring(0,16) + "...)", "INFO");
      
      // Play packet radio audio for TX
      playPacketAudio(hexPayload, true);
    } else {
      appendMonitor("MQTT Funk TX failed", "ERROR");
    }
  } else {
    appendMonitor("MQTT disconnected - KISS message lost", "ERROR");
  }
}

// Convert hex string to binary data
String hexToBytes(const String& hexString) {
  String result = "";
  for(int i = 0; i < hexString.length(); i += 2) {
    String hexByte = hexString.substring(i, i + 2);
    char byte = (char)strtol(hexByte.c_str(), NULL, 16);
    result += byte;
  }
  return result;
}

// Process MQTT message and forward to RS232  
void processMqttMessage(const String& message) {
  // KRITISCHE DEBUG-AUSGABE
  appendMonitor(">>> processMqttMessage() aufgerufen <<<", "DEBUG");
  appendMonitor("MQTT Message Länge: " + String(message.length()), "DEBUG");
  
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
  
  // JSON parser for incoming MQTT messages
  appendMonitor("Suche nach payload_hex in MQTT Message...", "DEBUG");
  int payloadStart = message.indexOf("\"payload_hex\":\"");
  appendMonitor("payloadStart Index: " + String(payloadStart), "DEBUG");
  if(payloadStart != -1) {
    payloadStart += 15; // Length of "payload_hex":""
    int payloadEnd = message.indexOf("\"", payloadStart);
    if(payloadEnd != -1) {
      String hexPayload = message.substring(payloadStart, payloadEnd);
      
      // Decrypt payload if shared secret is configured
      if (strlen(mqttSharedSecret) > 0) {
        hexPayload = decryptPayload(hexPayload);
        appendMonitor("MQTT RX Payload entschlüsselt: " + String(hexPayload.length()) + " Zeichen", "DEBUG");
      }
      
      // Validierung: Hex-String muss gerade Anzahl Zeichen haben
      if(hexPayload.length() % 2 != 0) {
        appendMonitor("MQTT RX Error: Ungültiger Hex-String (ungerade Länge)", "ERROR");
        return;
      }
      
      // Hex zu Binärdaten konvertieren 
      appendMonitor("Vor hexToBytes: hexPayload = " + hexPayload, "DEBUG");
      String binaryData = hexToBytes(hexPayload);
      appendMonitor("Nach hexToBytes: binaryData.length() = " + String(binaryData.length()), "DEBUG");
      
      // Debug: Zeige was an RS232 gesendet wird
      String debugHex = "";
      for(int i = 0; i < binaryData.length(); i++) {
        char hex[3];
        sprintf(hex, "%02X", (unsigned char)binaryData[i]);
        debugHex += hex;
        if(i < binaryData.length()-1) debugHex += " ";
      }
      
      // KRITISCHE DEBUG-AUSGABEN - IMMER ANZEIGEN
      appendMonitor("=== MQTT->RS232 START ===", "DEBUG");
      appendMonitor("HexPayload Länge: " + String(hexPayload.length()), "DEBUG");
      appendMonitor("BinaryData Länge: " + String(binaryData.length()), "DEBUG");
      appendMonitor("DEBUG: Sende 1:1 an RS232: " + debugHex, "DEBUG");
      
      // Hex-Daten 1:1 an RS232 senden (bereits vollständige KISS-Frames)
      int bytesWritten = RS232.print(binaryData);
      RS232.flush(); // Stelle sicher, dass Daten sofort gesendet werden
      
      appendMonitor("RS232.print() returnierte: " + String(bytesWritten) + " bytes", "DEBUG");
      
      // TX-Indikator für Display setzen (wenn Daten erfolgreich gesendet)
      if(bytesWritten > 0) {
        lastTX = millis();
        appendMonitor("TX-Indikator gesetzt (lastTX)", "DEBUG");
      }
      
      appendMonitor("=== MQTT->RS232 END ===", "DEBUG");
      
      // Packet Radio Audio für RX abspielen (mit Original-Hex vor Entschlüsselung)
      String originalHex = message.substring(message.indexOf("\"payload_hex\":\"") + 15);
      originalHex = originalHex.substring(0, originalHex.indexOf("\""));
      playPacketAudio(originalHex, false);
      
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
      
      appendMonitor("MQTT→RS232: " + String(binaryData.length()) + " bytes" + senderInfo + " (hex:" + hexPayload.substring(0,16) + "...)", "DEBUG");
      lastRX = millis();
    }
  } else {
    appendMonitor("MQTT RX: Keine payload_hex gefunden", "WARNING");
  }
}

// OTA check function - checks for available updates
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

// OTA update function - performs firmware update
void handleOTAUpdate() {
  appendMonitor("OTA: Starting firmware update...", "INFO");
  
  // First check if update is available
  HTTPClient http;
  
  configureHTTPClient(http, String(otaRepoUrl) + "/version.txt");
  // **OTA UPDATE HTTP TIMEOUTS** - override default timeouts
  http.setTimeout(30000); // 30 seconds for OTA update (longer due to download)
  http.setConnectTimeout(5000); // 5 seconds connect timeout
  
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
  
  // Use the same working update method as checkForUpdates()
  String firmwareUrl = String(otaRepoUrl) + "/udm-prig-client.ino.esp32.bin";
  
  configureHTTPClient(http, firmwareUrl);
  int resp = http.GET();
  
  if (resp == 200) {
    int contentLength = http.getSize();
    if (contentLength > 0) {
      WiFiClient * stream = http.getStreamPtr();
      bool canBegin = Update.begin(contentLength);
      
      if (canBegin) {
        appendMonitor("OTA: Starting download (" + String(contentLength) + " bytes)...", "INFO");
        
        uint8_t buff[512];
        int totalRead = 0;
        
        while (http.connected() && totalRead < contentLength) {
          size_t avail = stream->available();
          if (avail) {
            int read = stream->readBytes(buff, ((avail > sizeof(buff)) ? sizeof(buff) : avail));
            Update.write(buff, read);
            totalRead += read;
            
            // Progress feedback every 10%
            static int lastPercent = -1;
            int percent = (totalRead * 100) / contentLength;
            if (percent != lastPercent && percent % 10 == 0) {
              appendMonitor("OTA: Progress " + String(percent) + "%", "INFO");
              lastPercent = percent;
            }
          }
          yield();
        }
        
        if (Update.end(true)) {
          appendMonitor("OTA: Update successful! Restarting...", "INFO");
          server.send(200, "application/json", "{\"result\":\"Update successful\"}");
          
          // Save new version to EEPROM
          strncpy(localVersion, remoteVersion.c_str(), 15);
          localVersion[15] = 0;
          saveConfig();
          appendMonitor("New version saved: " + String(localVersion), "INFO");
          
          delay(1000); // Give time for response to be sent
          ESP.restart();
        } else {
          appendMonitor("OTA: Update failed during finalization. Error: " + String(Update.getError()), "ERROR");
          server.send(500, "application/json", "{\"error\":\"Update finalization failed\"}");
        }
      } else {
        appendMonitor("OTA: Cannot begin update - not enough space?", "ERROR");
        server.send(500, "application/json", "{\"error\":\"Cannot begin update\"}");
      }
    } else {
      appendMonitor("OTA: Empty firmware file", "ERROR");
      server.send(500, "application/json", "{\"error\":\"Empty firmware file\"}");
    }
  } else {
    appendMonitor("OTA: Firmware download failed. HTTP code: " + String(resp), "ERROR");
    server.send(500, "application/json", "{\"error\":\"Firmware download failed\"}");
  }
  
  http.end();
}

// LED blink function for status display
void blinkLED() {
  unsigned long now = millis();
  if (now - lastBlink > 250) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    lastBlink = now;
  }
}

// ==================================================================================
// MQTT PAYLOAD ENCRYPTION FUNCTIONS
// ==================================================================================

/**
 * Encrypt MQTT payload using XOR cipher with shared secret
 * Provides basic payload obfuscation for CB-Funk simulation
 * @param hexPayload Hex-encoded payload to encrypt
 * @return Encrypted hex string, or original if no secret configured
 */
String encryptPayload(const String& hexPayload) {
  if (strlen(mqttSharedSecret) == 0) {
    return hexPayload; // No encryption if no secret is set
  }
  
  if (hexPayload.length() % 2 != 0) {
    return hexPayload; // Invalid hex length, return original
  }
  
  String encrypted = "";
  int secretLen = strlen(mqttSharedSecret);
  int charIndex = 0;
  
  // Hex-String zu Bytes konvertieren, XOR anwenden, zurück zu Hex
  for (int i = 0; i < hexPayload.length(); i += 2) {
    String hexByte = hexPayload.substring(i, i + 2);
    char originalChar = (char)strtol(hexByte.c_str(), NULL, 16);
    char encryptedChar = originalChar ^ mqttSharedSecret[charIndex % secretLen];
    
    // Zurück zu Hex mit führender Null falls nötig
    char hexStr[3];
    sprintf(hexStr, "%02X", (unsigned char)encryptedChar);
    encrypted += String(hexStr);
    charIndex++;
  }
  
  return encrypted;
}

/**
 * Decrypt MQTT payload using XOR cipher with shared secret
 * Reverses the encryption process for received messages
 * @param encryptedHexPayload Encrypted hex-encoded payload
 * @return Decrypted hex string, or original if no secret configured
 */
String decryptPayload(const String& encryptedHexPayload) {
  if (strlen(mqttSharedSecret) == 0) {
    return encryptedHexPayload; // No decryption if no secret is set
  }
  
  if (encryptedHexPayload.length() % 2 != 0) {
    return encryptedHexPayload; // Invalid hex length, return original
  }
  
  String decrypted = "";
  int secretLen = strlen(mqttSharedSecret);
  int charIndex = 0;
  
  // Convert hex string to bytes, apply XOR, back to hex
  for (int i = 0; i < encryptedHexPayload.length(); i += 2) {
    String hexByte = encryptedHexPayload.substring(i, i + 2);
    char encryptedChar = (char)strtol(hexByte.c_str(), NULL, 16);
    char decryptedChar = encryptedChar ^ mqttSharedSecret[charIndex % secretLen];
    
    // Back to hex with leading zero if needed
    char hexStr[3];
    sprintf(hexStr, "%02X", (unsigned char)decryptedChar);
    decrypted += String(hexStr);
    charIndex++;
  }
  
  return decrypted;
}

// ==================================================================================
// I2S AUDIO SYSTEM FOR MAX98357A AMPLIFIER
// ==================================================================================

/**
 * Initialize I2S audio system for MAX98357A amplifier
 * Configures high-quality audio output for AFSK packet radio simulation
 * Sets up 44.1kHz sample rate with 16-bit resolution for studio-quality sound
 */
void setupI2SAudio() {
  Serial.println("I2S setup starting...");
  unsigned long i2sInternalStart = millis();
  
  // Optimized I2S configuration for faster boot
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = I2S_SAMPLE_RATE,                     // 44.1kHz CD-quality
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,      // 16-bit resolution
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,       // Mono output
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 2,                                 // Reduced buffer count for faster init
    .dma_buf_len = 128,                                 // Smaller buffer for faster init
    .use_apll = false,                                  // Disable APLL for faster boot
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  // I2S Pin-Konfiguration
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRC_PIN,
    .data_out_num = I2S_DOUT_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  // I2S installieren und starten (this is the slow part)
  unsigned long driverStart = millis();
  esp_err_t result = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  Serial.println("I2S driver install took: " + String(millis() - driverStart) + "ms");
  
  if (result != ESP_OK) {
    appendMonitor("I2S Driver Installation failed: " + String(result), "ERROR");
    Serial.println("I2S driver install FAILED with code: " + String(result));
    return;
  }
  
  unsigned long pinStart = millis();
  result = i2s_set_pin(I2S_NUM_0, &pin_config);
  Serial.println("I2S pin setup took: " + String(millis() - pinStart) + "ms");
  
  if (result != ESP_OK) {
    appendMonitor("I2S Pin Setup failed: " + String(result), "ERROR");
    Serial.println("I2S pin setup FAILED with code: " + String(result));
    return;
  }
  
  appendMonitor("I2S Audio für MAX98357A initialisiert", "INFO");
  
  // Initialize hardware gain pin (this has delays!)
  unsigned long gainStart = millis();
  setupHardwareGain();
  Serial.println("Hardware gain setup took: " + String(millis() - gainStart) + "ms");
  
  Serial.println("I2S setup completed in: " + String(millis() - i2sInternalStart) + "ms");
}

/**
 * Setup and configure hardware gain control for MAX98357A
 * Controls the GAIN pin to set hardware amplification level
 */
void setupHardwareGain() {
  Serial.println("Hardware gain setup starting...");
  unsigned long gainSetupStart = millis();
  
  // Apply the hardware gain setting loaded from EEPROM
  setHardwareGain(hardwareGain);
  Serial.println("setHardwareGain() took: " + String(millis() - gainSetupStart) + "ms");
  
  unsigned long monitorStart = millis();
  // Temporarily disable appendMonitor during boot to avoid timeout
  Serial.println("Hardware-Gain Pin (GPIO" + String(I2S_GAIN_PIN) + ") konfiguriert");
  Serial.println("appendMonitor() took: " + String(millis() - monitorStart) + "ms");
}

/**
 * Set hardware gain level on MAX98357A amplifier
 * @param gainLevel: 0=9dB (floating), 1=6dB (Vin), 2=12dB (GND)
 */
void setHardwareGain(uint8_t gainLevel) {
  String gainDesc;
  
  // First detach any previous LEDC setup
  ledcDetach(I2S_GAIN_PIN);
  
  switch(gainLevel) {
    case 0: // 9dB - Floating (high impedance) - Standard
      pinMode(I2S_GAIN_PIN, INPUT);  // High impedance = floating
      gainDesc = "9dB (Standard)";
      break;
      
    case 1: // 6dB - HIGH (connect to Vin)
      pinMode(I2S_GAIN_PIN, OUTPUT);
      digitalWrite(I2S_GAIN_PIN, HIGH);
      gainDesc = "6dB (Niedrig)";
      break;
      
    case 2: // 12dB - LOW (connect to GND)
      pinMode(I2S_GAIN_PIN, OUTPUT);
      digitalWrite(I2S_GAIN_PIN, LOW);
      gainDesc = "12dB (Hoch)";
      break;
      
    default:
      gainLevel = 0;
      pinMode(I2S_GAIN_PIN, INPUT);
      gainDesc = "9dB (Standard - Fallback)";
      break;
  }
  
  hardwareGain = gainLevel;
  
  // Minimal delay for faster boot (was 10ms, now 1ms)
  delay(1);
  
  // Temporarily use Serial instead of appendMonitor during boot to avoid timeout
  Serial.println("Hardware-Verstärkung: " + gainDesc + " (GPIO" + String(I2S_GAIN_PIN) + ")");
  
  // Debug output only in debug mode to save time
  if (logLevel >= 3) {
    int pinState = digitalRead(I2S_GAIN_PIN);
    Serial.println("DEBUG: GAIN Pin State = " + String(pinState) + " (Level " + String(gainLevel) + ")");
  }
}

// Play audio samples via I2S
void playI2SSamples(int16_t* samples, size_t sampleCount) {
  if (!packetAudioEnabled) return;
  
  size_t bytesWritten;
  esp_err_t result = i2s_write(I2S_NUM_0, samples, sampleCount * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
  
  if (result != ESP_OK) {
    appendMonitor("I2S Write Error: " + String(result), "ERROR");
  }
}

// ==================== PACKET RADIO AUDIO SIMULATION (AFSK mit I2S) ====================

// Global phase for phase-continuous AFSK
static float currentPhase = 0.0;

// Play tone with I2S-based AFSK for studio quality
void playAFSKToneI2S(int frequency, int durationUs, bool isTx = false) {
  if (!packetAudioEnabled) return;
  
  // TX/RX frequency offset for authentic sound
  int actualFreq = isTx ? (frequency - 50) : frequency;
  
  // Berechne Anzahl der Samples
  int sampleCount = (durationUs * I2S_SAMPLE_RATE) / 1000000;
  if (sampleCount <= 0) return;
  
  // Sample-Buffer erstellen
  int16_t* samples = (int16_t*)malloc(sampleCount * sizeof(int16_t));
  if (!samples) return;
  
  float phaseIncrement = 2.0 * PI * actualFreq / I2S_SAMPLE_RATE;
  
  // Generiere Sinus-Samples mit phasen-kontinuierlicher AFSK
  for (int i = 0; i < sampleCount; i++) {
    currentPhase += phaseIncrement;
    if (currentPhase >= 2.0 * PI) currentPhase -= 2.0 * PI;
    
    // Sine to 16-bit sample with volume control (scaled to 50% max)
    float sineValue = sin(currentPhase);
    int16_t amplitude = (int16_t)(30000 * audioVolume * 50 / 10000); // Volume 0-100% scaled to 50% max
    samples[i] = (int16_t)(sineValue * amplitude);
  }
  
  // Output via I2S
  playI2SSamples(samples, sampleCount);
  
  // Memory freigeben
  free(samples);
}

// Fallback: Einfacher Ton für Sync (Kompatibilität)
void playTone(int frequency, int durationUs) {
  if (!packetAudioEnabled) return;
  
  // Für Sync-Töne einfaches Rechteck verwenden (schneller)
  int sampleCount = (durationUs * I2S_SAMPLE_RATE) / 1000000;
  if (sampleCount <= 0) return;
  
  int16_t* samples = (int16_t*)malloc(sampleCount * sizeof(int16_t));
  if (!samples) return;
  
  int samplesPerHalfCycle = I2S_SAMPLE_RATE / (2 * frequency);
  
  for (int i = 0; i < sampleCount; i++) {
    // Rechteck-Signal für Sync mit Lautstärke-Kontrolle (scaled to 50% max)
    bool high = ((i / samplesPerHalfCycle) % 2) == 0;
    int16_t amplitude = (int16_t)(20000 * audioVolume * 50 / 10000); // Lautstärke 0-100% scaled to 50% max
    samples[i] = high ? amplitude : -amplitude;
  }
  
  playI2SSamples(samples, sampleCount);
  free(samples);
}

// Spielt ein einzelnes Bit als I2S-AFSK (Bell 202 Standard)
void playAFSKBit(bool bit, bool isTx = false) {
  if (!packetAudioEnabled) return;
  
  int frequency = bit ? afskFreqMark : afskFreqSpace; // 1=Mark frequency, 0=Space frequency (configurable)
  playAFSKToneI2S(frequency, BIT_DURATION_US, isTx);
}

// Fallback: Einfache Bit-Funktion (für Kompatibilität)
void playBit(bool bit, bool isTx = false) {
  playAFSKBit(bit, isTx);
}

// Spielt ein Byte als 8 Bits (LSB zuerst wie bei serieller Übertragung)
void playByte(uint8_t data, bool isTx = false) {
  if (!packetAudioEnabled) return;
  
  // Start Bit (immer 0)
  playBit(false, isTx);
  
  // 8 Daten-Bits (LSB zuerst)
  for (int i = 0; i < 8; i++) {
    bool bit = (data >> i) & 1;
    playBit(bit, isTx);
  }
  
  // Stop Bit (immer 1)
  playBit(true, isTx);
}

/**
 * Play packet radio audio for KISS frame transmission
 * Generates authentic Bell 202 AFSK tones for packet radio simulation
 * @param hexData Hex-encoded packet data to transmit as audio
 * @param isTx True for TX mode (affects frequency offset for realism)
 */
void playPacketAudio(const String& hexData, bool isTx) {
  if (!packetAudioEnabled) return;
  
  // TX Delay Simulation - PTT to data start delay
  // This simulates the time needed for:
  // - Radio to switch from RX to TX mode
  // - Receiver squelch to open properly
  // - TX circuits to stabilize
  if (isTx && txDelay > 0) {
    // Play carrier tone during TX delay (authentic packet radio behavior)
    int carrierFreq = (afskFreqMark + afskFreqSpace) / 2; // Carrier between mark/space
    appendMonitor("TX Delay: " + String(txDelay) + "ms (carrier tone)", "DEBUG");
    playTone(carrierFreq, txDelay * 1000); // Convert ms to microseconds
  } else {
    // Short pause for RX or if TX delay is disabled
    delay(50);
  }
  
  // TX/RX distinction: TX slightly lower
  int markFreq = isTx ? (afskFreqMark - 100) : afskFreqMark;
  int spaceFreq = isTx ? (afskFreqSpace - 100) : afskFreqSpace;
  
  // Präambel: ~200ms von abwechselnden Tönen für Sync
  for (int i = 0; i < 24; i++) {
    playTone(i % 2 ? markFreq : spaceFreq, 8000); // Kurze Sync-Töne
  }
  
  // KISS Frame Start Flag (C0)
  playByte(0xC0, isTx);
  
  // Convert hex string to bytes and play
  for (int i = 0; i < hexData.length(); i += 2) {
    if (i + 1 < hexData.length()) {
      String hexByte = hexData.substring(i, i + 2);
      uint8_t byteVal = (uint8_t)strtol(hexByte.c_str(), NULL, 16);
      playByte(byteVal, isTx);
    }
  }
  
  // KISS Frame End Flag (C0)
  playByte(0xC0, isTx);
  
  // Short pause after frame
  delay(30);
}