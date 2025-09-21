#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Fonts/Org_01.h>
#include <Fonts/TomThumb.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <ESPmDNS.h>
#include <time.h>
#include <FS.h>
#include <LittleFS.h>

// ========== OTA-Konfiguration ==========
const char* FIRMWARE_VERSION_PATH = "/version.txt";  // Muss auf LittleFS liegen
const char* FIRMWARE_BIN_PATH     = "/firmware.bin"; // Muss auf LittleFS liegen

// ========== Konfiguration ==========
#define EEPROM_SIZE 1200
#define SSID_OFFSET 0
#define PASS_OFFSET 64
#define CALLSIGN_OFFSET 160
#define BAUD_OFFSET 194
#define LOGLEVEL_OFFSET 198
#define CLIENTLIST_OFFSET 201
#define MAX_CLIENTS 8
#define CLIENT_ENTRY_SIZE (16+16+2+1)

#define LED_PIN 2
#define RX_BUF_SIZE 1024  // Optimiert: größerer Buffer für höhere Durchsatzrate

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_LINE_LEN 33  // Displaybreite in Zeichen
#define OLED_LINE_DIVIDER "---------------------------------"

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define AX25_LINES 8
String ax25Lines[AX25_LINES];
uint8_t ax25LineIdx = 0;

#define MONITOR_BUF_SIZE 4096
String monitorBuf = "";

char wifiSsid[64] = "";
char wifiPass[64] = "";
char callsign[32] = "";
uint32_t baudrate = 2400;
uint8_t logLevel = 1; // 0=Error, 1=Info, 2=Warning, 3=Debug

WebServer webserver(80);

WiFiServer server(22222);
WiFiClient* clients[MAX_CLIENTS] = {nullptr};
String clientCallsigns[MAX_CLIENTS];
unsigned long clientLastActivity[MAX_CLIENTS];

uint8_t rxbuf[RX_BUF_SIZE];   // Buffer für AX25 von Clients -> RS232
size_t rxcnt = 0;

uint8_t txbuf[RX_BUF_SIZE];   // Buffer für AX25 von RS232 -> Clients
size_t txcnt = 0;

HardwareSerial RS232(1); // UART1 (RX=16, TX=17)
unsigned long lastBlink = 0;
bool ledState = false;
unsigned long lastStatusUpdate = 0;
unsigned long lastMonitorUpdate = 0;
bool monitorDirty = false;

unsigned long clientTimeoutMs = 60000; // 60 Sek

// ====== NTP Config ======
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

struct ax25_ctrl_desc {
  uint8_t code;
  const char* shortname;
  const char* longname;
};

static const ax25_ctrl_desc ax25_ctrl_codes[] = {
  {0x2F, "SABM+", "Set Asynchronous Balanced Mode Extended"},
  {0x43, "DISC+", "Disconnect Extended"},
  {0x63, "UA+",   "Unnumbered Acknowledgement Extended"},
  {0x0F, "DM+",   "Disconnected Mode Extended"},
  {0x87, "SABM",  "Set Asynchronous Balanced Mode"},
  {0xC3, "DISC",  "Disconnect"},
  {0xE3, "UA",    "Unnumbered Acknowledgement"},
  {0x1F, "DM",    "Disconnected Mode"},
  {0x03, "UI",    "Unnumbered Information"},
  {0x13, "UI",    "Unnumbered Information (P/F set)"},
  {0x0B, "FRMR",  "Frame Reject"},
  {0xEF, "SNRM",  "Set Normal Response Mode"},
  {0xAB, "XID",   "Exchange Identification"},
  {0x01, "RR",    "Receive Ready"},
  {0x11, "RNR",   "Receive Not Ready"},
  {0x21, "REJ",   "Reject"},
  {0x31, "SREJ",  "Selective Reject"}
};




// ====== Clientlist-Struktur und EEPROM-Handling ======
struct ClientEntry {
  char callsign[16];
  uint16_t port;
  uint8_t status; // 0=allow, 1=deny, 2=blocked
};
ClientEntry clientList[MAX_CLIENTS];

void blinkLED();

String getTimestamp() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 50)) {
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

void appendMonitor(const String& msg, const char* level = "INFO") {
  String line = "[";
  line += getTimestamp();
  line += "] [";
  line += level;
  line += "] ";
  line += msg;
  line += "\n";
  monitorBuf += line;
  if (monitorBuf.length() > MONITOR_BUF_SIZE) {
    monitorBuf = monitorBuf.substring(monitorBuf.length() - MONITOR_BUF_SIZE);
  }
}

void loadClientList() {
  for (int i=0; i<MAX_CLIENTS; ++i) {
    int base = CLIENTLIST_OFFSET + i*CLIENT_ENTRY_SIZE;
    for (int j=0; j<16; ++j) clientList[i].callsign[j] = EEPROM.read(base+j);
    clientList[i].port = (EEPROM.read(base+32)<<8)|EEPROM.read(base+33);
    clientList[i].status = EEPROM.read(base+34);
  }
  appendMonitor("Client-Liste geladen", "INFO");
}

void saveClientList() {
  for (int i=0; i<MAX_CLIENTS; ++i) {
    int base = CLIENTLIST_OFFSET + i*CLIENT_ENTRY_SIZE;
    for (int j=0; j<16; ++j) EEPROM.write(base+j, clientList[i].callsign[j]);
    EEPROM.write(base+32, (clientList[i].port>>8)&0xFF);
    EEPROM.write(base+33, clientList[i].port&0xFF);
    EEPROM.write(base+34, clientList[i].status);
  }
  EEPROM.commit();
  appendMonitor("Client-Liste gespeichert", "INFO");
}

void handleClientListJSON() {
  String json = "[";
  for (int i=0; i<MAX_CLIENTS; ++i) {
    if (i>0) json += ",";
    json += "{";
    json += "\"id\":"+String(i);
    json += ",\"callsign\":\""+String(clientList[i].callsign)+"\"";
    json += ",\"port\":"+String(clientList[i].port);
    json += ",\"status\":"+String(clientList[i].status);
    json += "}";
  }
  json += "]";
  webserver.send(200, "application/json", json);
}

void handleClientListSave() {
  int id = webserver.arg("id").toInt();
  if (id<0 || id>=MAX_CLIENTS) {
    for (id=0; id<MAX_CLIENTS; ++id)
      if (clientList[id].callsign[0]==0) break;
    if (id>=MAX_CLIENTS) { webserver.send(400,"text/plain","FULL"); return; }
  }
  String callsign = webserver.arg("callsign");
  uint16_t port = webserver.arg("port").toInt();
  uint8_t status = webserver.arg("status").toInt();
  strncpy(clientList[id].callsign, callsign.c_str(), 15); clientList[id].callsign[15]=0;
  clientList[id].port = port;
  clientList[id].status = status;
  saveClientList();
  appendMonitor("Client-Eintrag gespeichert: " + callsign, "INFO");
  webserver.send(200,"text/plain","");
}

void handleClientListDelete() {
  int id = webserver.arg("id").toInt();
  if (id<0 || id>=MAX_CLIENTS) { webserver.send(400,"text/plain",""); return; }
  appendMonitor("Client-Eintrag gelöscht: " + String(clientList[id].callsign), "INFO");
  clientList[id].callsign[0]=0;
  clientList[id].port=0;
  clientList[id].status=0;
  saveClientList();
  webserver.send(200,"text/plain","");
}

void handleMonitor() {
  webserver.send(200, "text/plain", monitorBuf);
}

void handleMonitorClear() {
  monitorBuf = "";
  appendMonitor("Monitor gelöscht", "INFO");
  webserver.send(200, "text/plain", "OK");
}

// ===== OLED Boot-Log =====
void bootPrint(const String &msg) {
  static int line = 0;
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 8 * line++);
  display.print(msg);
  display.display();
  if (line >= 8) {
    display.clearDisplay();
    line = 0;
  }
  delay(300);
}

// ======= Hilfsfunktionen für OLED/AX25 =======
void decode_ax25_addr(char* out, const uint8_t* in) {
  int j = 0;
  for (int i = 0; i < 6; i++) {
    char c = (in[i] >> 1);
    if (c != ' ') out[j++] = c;
  }
  out[j] = 0;
}

void ax25OledPrint(String msg) {
  while (msg.length() > 0) {
    String line = msg.substring(0, OLED_LINE_LEN);
    ax25Lines[ax25LineIdx] = line;
    ax25LineIdx = (ax25LineIdx + 1) % AX25_LINES;
    msg = msg.substring(line.length());
  }
  // Horizontale Linie als Trennung
  ax25Lines[ax25LineIdx] = OLED_LINE_DIVIDER;
  ax25LineIdx = (ax25LineIdx + 1) % AX25_LINES;
  monitorDirty = true;
}

const char* ax25_ctrl_format(uint8_t ctrl) {
  // I-Frame: LSB ist 0
  if ((ctrl & 0x01) == 0)
    return "I (Information Frame)";
  
  // S-Frames: unteres Nibble ist 1, 5, 9, D; oberes Nibble = NR<<5
  switch (ctrl & 0x0F) {
    case 0x01: return "RR (Receive Ready)";
    case 0x05: return "RR (Receive Ready)";
    case 0x09: return "RR (Receive Ready)";
    case 0x0D: return "RR (Receive Ready)";
    case 0x11: return "RNR (Receive Not Ready)";
    case 0x15: return "RNR (Receive Not Ready)";
    case 0x19: return "RNR (Receive Not Ready)";
    case 0x1D: return "RNR (Receive Not Ready)";
    case 0x21: return "REJ (Reject)";
    case 0x25: return "REJ (Reject)";
    case 0x29: return "REJ (Reject)";
    case 0x2D: return "REJ (Reject)";
    case 0x31: return "SREJ (Selective Reject)";
    case 0x35: return "SREJ (Selective Reject)";
    case 0x39: return "SREJ (Selective Reject)";
    case 0x3D: return "SREJ (Selective Reject)";
  }

  // U-Frames: untere 5 Bit + P/F-Bit (Bit 4) ignorieren!
  switch (ctrl & 0xEF) {
    case 0x2F: return "SABM+ (Set Asynchronous Balanced Mode Extended)";
    case 0x43: return "DISC+ (Disconnect Extended)";
    case 0x63: return "UA+ (Unnumbered Acknowledgement Extended)";
    case 0x0F: return "DM+ (Disconnected Mode Extended)";
    case 0x87: return "SABM (Set Asynchronous Balanced Mode)";
    case 0xC3: return "DISC (Disconnect)";
    case 0xE3: return "UA (Unnumbered Acknowledgement)";
    case 0x1F: return "DM (Disconnected Mode)";
    case 0x03: return "UI (Unnumbered Information)";
    case 0x0B: return "FRMR (Frame Reject)";
    case 0xEF: return "SNRM (Set Normal Response Mode)";
    case 0xAB: return "XID (Exchange Identification)";
  }

  // U-Frames mit P/F-Bit gesetzt (Bit 4 = 1), extra abdecken:
  switch (ctrl & 0xEF) {
    case 0x3F: return "SABM+ (Set Asynchronous Balanced Mode Extended)";
    case 0x53: return "DISC+ (Disconnect Extended)";
    case 0x73: return "UA+ (Unnumbered Acknowledgement Extended)";
    case 0x1F: return "DM+ (Disconnected Mode Extended)";
  }

  static char unkbuf[32];
  snprintf(unkbuf, sizeof(unkbuf), "CTRL 0x%02X (Unknown)", ctrl);
  return unkbuf;
}

void printAX25Packet(const uint8_t* buf, size_t len, bool incoming) {
  if (len < 15) {
    String errMsg = incoming ? "AX25 zu kurz" : "AX25 zu kurz (gesendet)";
    ax25OledPrint(errMsg);
    appendMonitor(errMsg, "WARNING");
    return;
  }
  char dest[7] = {0}, src[7] = {0};
  decode_ax25_addr(dest, buf);
  decode_ax25_addr(src, buf+7);

  uint8_t control = buf[14];
  size_t infoStart = 15;
  if (control == 0x03 && len >= 16) infoStart = 16; // UI-Frame

  size_t infoLen = (len > infoStart) ? (len - infoStart) : 0;
  const uint8_t* info = buf + infoStart;

  String sLine = String(src) + ">" + String(dest) + (incoming ? ": " : ":\n");
  if (infoLen == 0) {
    sLine += String("[") + ax25_ctrl_format(control) + "]";
  } else {
    for (size_t i = 0; i < infoLen; ++i) {
      if (info[i] >= 32 && info[i] <= 126)
        sLine += (char)info[i];
    }
  }
  ax25OledPrint(sLine);
  appendMonitor(sLine, "AX25");
}


size_t kiss_unescape(const uint8_t* in, size_t len, uint8_t* out) {
  size_t outlen = 0;
  for (size_t i = 0; i < len; i++) {
    if (in[i] == 0xDB) {
      i++;
      if (i >= len) break;
      if (in[i] == 0xDC) out[outlen++] = 0xC0;
      else if (in[i] == 0xDD) out[outlen++] = 0xDB;
      else out[outlen++] = in[i];
    } else {
      out[outlen++] = in[i];
    }
  }
  return outlen;
}

void drawAntenna(int x, int y) {
  display.drawLine(x+2, y+3, x+2, y+6, SH110X_WHITE);
  display.drawLine(x+2, y+3, x, y, SH110X_WHITE);
  display.drawLine(x+2, y+3, x+4, y, SH110X_WHITE);
}

void drawStatusBarSmallBars(int rssi, int bars) {
  display.setFont(&Org_01);
  display.setTextColor(SH110X_WHITE);
  display.fillRect(0, 0, SCREEN_WIDTH, 10, SH110X_BLACK);

  String ip = WiFi.isConnected() ? WiFi.localIP().toString() : String("0.0.0.0");
  display.setCursor(0, 6);
  display.print(ip);

  String cStr = "   CLIENTS: ";
  int clientCount = 0;
  for (int i=0;i<MAX_CLIENTS;i++) if (clients[i] && clients[i]->connected()) clientCount++;
  cStr += clientCount;
  int xcenter = (SCREEN_WIDTH - (cStr.length() * 6)) / 2;
  display.setCursor(xcenter, 6);
  display.print(cStr);

  drawAntenna(SCREEN_WIDTH - 12, 0);

  display.fillRect(SCREEN_WIDTH - 6, 2, 6, 6, SH110X_BLACK);
  int xb = SCREEN_WIDTH - 6;
  for (int i = 0; i < 3; i++) {
    int h = 2 + 2*i;
    int yb = 7 - h + 0;
    if (i < bars) display.fillRect(xb + 2*i, yb, 2, h, SH110X_WHITE);
    else display.drawRect(xb + 2*i, yb, 2, h, SH110X_WHITE);
  }
  display.drawLine(0, 8, SCREEN_WIDTH-1, 8, SH110X_WHITE);
}

void updateMonitorOnly() {
  display.fillRect(0, 9, SCREEN_WIDTH, SCREEN_HEIGHT-9, SH110X_BLACK);
  display.setFont(&TomThumb);
  display.setTextColor(SH110X_WHITE);
  for (int i = 0; i < AX25_LINES; i++) {
    int idx = (ax25LineIdx + i) % AX25_LINES;
    display.setCursor(0, 19 + 6 * i);
    display.print(ax25Lines[idx]);
  }
  display.display();
}

// ============ EEPROM Config ============

void saveConfig() {
  for (int i = 0; i < 64; ++i) EEPROM.write(SSID_OFFSET+i, wifiSsid[i]);
  for (int i = 0; i < 64; ++i) EEPROM.write(PASS_OFFSET+i, wifiPass[i]);
  for (int i = 0; i < 32; ++i) EEPROM.write(CALLSIGN_OFFSET+i, callsign[i]);
  EEPROM.write(BAUD_OFFSET, (baudrate >> 24) & 0xFF);
  EEPROM.write(BAUD_OFFSET+1, (baudrate >> 16) & 0xFF);
  EEPROM.write(BAUD_OFFSET+2, (baudrate >> 8) & 0xFF);
  EEPROM.write(BAUD_OFFSET+3, baudrate & 0xFF);
  EEPROM.write(LOGLEVEL_OFFSET, logLevel);
  EEPROM.commit();
  appendMonitor("Konfiguration gespeichert", "INFO");
}

void loadConfig() {
  for (int i = 0; i < 64; ++i) wifiSsid[i] = EEPROM.read(SSID_OFFSET+i);
  wifiSsid[63] = 0;
  for (int i = 0; i < 64; ++i) wifiPass[i] = EEPROM.read(PASS_OFFSET+i);
  wifiPass[63] = 0;
  for (int i = 0; i < 32; ++i) callsign[i] = EEPROM.read(CALLSIGN_OFFSET+i);
  callsign[31] = 0;
  baudrate = ((uint32_t)EEPROM.read(BAUD_OFFSET) << 24)
           | ((uint32_t)EEPROM.read(BAUD_OFFSET+1) << 16)
           | ((uint32_t)EEPROM.read(BAUD_OFFSET+2) << 8)
           | ((uint32_t)EEPROM.read(BAUD_OFFSET+3));
  logLevel = EEPROM.read(LOGLEVEL_OFFSET);
  if(baudrate == 0xFFFFFFFF || baudrate == 0x00000000) {
    baudrate = 2400;
    appendMonitor("Baudrate war ungültig, auf 2400 gesetzt.", "WARNING");
  }
  if(logLevel > 3) logLevel = 1;
  appendMonitor("Konfiguration geladen", "INFO");
}

// ============ Authentifizierung ============

bool isCallsignAllowed(const String& call) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (String(clientList[i].callsign) == call) {
      if (clientList[i].status == 0) return true; // allow
      else return false; // deny/blocked
    }
  }
  return false;
}

// ============ OTA Webserver Handler ============
void handleFirmwareVersion() {
  File vfile = LittleFS.open(FIRMWARE_VERSION_PATH, "r");
  if(!vfile) { webserver.send(404, "text/plain", "version.txt fehlt"); return; }
  String version = vfile.readString(); vfile.close();
  version.trim();
  webserver.send(200, "text/plain", version);
}

void handleFirmwareBin() {
  File fw = LittleFS.open(FIRMWARE_BIN_PATH, "r");
  if(!fw) { webserver.send(404, "text/plain", "firmware.bin fehlt"); return; }
  webserver.streamFile(fw, "application/octet-stream");
  fw.close();
}



// ============ Webserver/Config ============

void handleRoot() {
  String html = "<!DOCTYPE html>\n<html>\n<head>\n";
  html += "<meta charset=\"UTF-8\">\n";
  html += "<title>UDMPRIG-Server</title>\n";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"/>\n";
  html += "<link href=\"https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/css/materialize.min.css\" rel=\"stylesheet\">\n";
  html += "<style>body {padding:18px;} pre {font-size: 12px; background:#222; color:#eee; padding:10px;} .input-field label {color: #009688;} .tabs .tab a.active { color: #009688;} .tabs .tab a { color: #444;} .input-field {margin-bottom: 0;} .custom-row {margin-bottom: 20px;} .modal-content { text-align: center; } .modal .preloader-wrapper { margin: 30px auto; } .modal .errormsg { color: #b71c1c; font-weight: bold; font-size: 1.2em; } td input, td select {width:95%; margin:0; font-size:1em;} td {vertical-align:middle;} .icon-btn {border:none; background:transparent; cursor:pointer;}</style>\n";
  html += "</head>\n<body>\n";
  html += "<h4><i class=\"material-icons left\">settings_ethernet</i>UDMPRIG-Server</h4>\n";
  html += "<ul id=\"tabs-swipe-demo\" class=\"tabs\">\n";
  html += "<li class=\"tab col s3\"><a class=\"active\" href=\"#config\">Config</a></li>\n";
  html += "<li class=\"tab col s3\"><a href=\"#monitor\">Monitor</a></li>\n";
  html += "</ul>\n";
  html += "<div id=\"config\" class=\"col s12\">\n";
  html += "<form id=\"configform\" action='/save' method='post'>\n";
  html += "<div class=\"input-field custom-row\">\n";
  html += "<input id=\"ssid\" name=\"ssid\" type=\"text\" maxlength=\"63\" value=\"" + String(wifiSsid) + "\">\n";
  html += "<label for=\"ssid\" class=\"active\">WLAN SSID</label>\n";
  html += "</div>\n";
  html += "<div class=\"input-field custom-row\">\n";
  html += "<input id=\"pass\" name=\"pass\" type=\"password\" maxlength=\"63\" value=\"" + String(wifiPass) + "\">\n";
  html += "<label for=\"pass\" class=\"active\">WLAN Passwort</label>\n";
  html += "</div>\n";
  html += "<div class=\"input-field custom-row\">\n";
  html += "<input id=\"callsign\" name=\"callsign\" type=\"text\" maxlength=\"31\" value=\"" + String(callsign) + "\">\n";
  html += "<label for=\"callsign\" class=\"active\">Callsign</label>\n";
  html += "</div>\n";
  html += "<div class=\"input-field custom-row\">\n";
  html += "<select id=\"baudrate\" name=\"baudrate\">\n";
  uint32_t rates[] = {1200, 2400, 4800, 9600, 14400, 19200, 38400, 57600, 115200};
  for(int i=0;i<9;++i){
    html += "<option value='" + String(rates[i]) + "'";
    if(baudrate==rates[i]) html += " selected";
    html += ">" + String(rates[i]) + " Baud</option>\n";
  }
  html += "</select>\n";
  html += "<label for=\"baudrate\">RS232 Baudrate</label>\n";
  html += "</div>\n";
  html += "<div class=\"input-field custom-row\">\n";
  html += "<select id=\"loglevel\" name=\"loglevel\">\n";
  const char* logNames[] = {"Error", "Info", "Warning", "Debug"};
  for(int i=0; i<4; ++i) {
    html += "<option value=\"" + String(i) + "\"";
    if(logLevel==i) html += " selected";
    html += ">" + String(logNames[i]) + "</option>\n";
  }
  html += "</select>\n";
  html += "<label for=\"loglevel\">Log Level</label>\n";
  html += "</div>\n";
  html += "<button class=\"btn waves-effect waves-light teal\" type=\"submit\" id=\"savebtn\">Speichern\n";
  html += "<i class=\"material-icons right\">save</i>\n";
  html += "</button>\n</form>\n";
  html += "<div id=\"clientlist-section\">\n";
  html += "<h6 style=\"margin-top:2em;\">Client-Liste</h6>\n";
  html += "<table class=\"striped\" id=\"clienttbl\">\n";
  html += "<thead><tr><th>#</th><th>Callsign</th><th>Port</th><th>Status</th><th>Aktion</th></tr></thead>\n";
  html += "<tbody id=\"clientbody\"></tbody>\n";
  html += "<tfoot><tr><td></td>\n";
  html += "<td><input id=\"new_callsign\" maxlength=\"15\"></td>\n";
  html += "<td><input id=\"new_port\" type=\"number\" min=\"1\" max=\"65535\"></td>\n";
  html += "<td><select id=\"new_status\"><option value=\"0\">allow</option><option value=\"1\">deny</option><option value=\"2\">blocked</option></select></td>\n";
  html += "<td><button class=\"icon-btn\" onclick=\"addRow()\"><i class='material-icons green-text'>save</i></button></td>\n";
  html += "</tr></tfoot></table>\n</div>\n";
  html += "<div class=\"section\">(c) www.pukepals.com, 73 de AT1NAD<br>\n";
  html += "<small>Auch erreichbar unter: <b>http://udmprig-server.local/</b></small></div>\n";
  html += "<div id=\"restartModal\" class=\"modal\">\n";
  html += "<div class=\"modal-content\"><h5>Server wird neu gestartet</h5>\n";
  html += "<div class=\"preloader-wrapper big active\" id=\"spinner\">\n";
  html += "<div class=\"spinner-layer spinner-blue-only\">\n";
  html += "<div class=\"circle-clipper left\"><div class=\"circle\"></div></div>\n";
  html += "<div class=\"gap-patch\"><div class=\"circle\"></div></div>\n";
  html += "<div class=\"circle-clipper right\"><div class=\"circle\"></div></div>\n";
  html += "</div></div><div id=\"restartMsg\"><p>Bitte warten...</p></div></div></div>\n";
  html += "</div>\n";
  // --- Monitor Tab ---
  html += "<div id=\"monitor\" class=\"col s12\">\n";
  html += "<h6>Server Monitor (inkl. AX.25, System, Debug)</h6>\n";
  html += "<pre id=\"monitorArea\" style=\"height:350px;overflow:auto;background:#222;color:#eee;\"></pre>\n";
  html += "<button class=\"btn red\" onclick=\"clearMonitor()\">Leeren</button>\n";
  html += "<script>\n";
  html += "function updateMonitor() {\n";
  html += "  fetch('/monitor').then(r=>r.text()).then(t=>{\n";
  html += "    let areaDiv = document.getElementById('monitorArea');\n";
  html += "    let atBottom = (areaDiv.scrollTop + areaDiv.clientHeight >= areaDiv.scrollHeight-2);\n";
  html += "    areaDiv.innerHTML = t.replace(/\\n/g,'<br>');\n";
  html += "    if(atBottom) areaDiv.scrollTop = areaDiv.scrollHeight;\n";
  html += "  });\n";
  html += "}\n";
  html += "setInterval(updateMonitor, 1000);\n";
  html += "function clearMonitor() { fetch('/monitor_clear').then(()=>updateMonitor()); }\n";
  html += "window.onload = updateMonitor;\n";
  html += "</script>\n";
  html += "</div>\n";
  html += "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/js/materialize.min.js\"></script>\n";
  html += "<link href=\"https://fonts.googleapis.com/icon?family=Material+Icons\" rel=\"stylesheet\">\n";
  html += "<script>\n";
  html += "document.addEventListener('DOMContentLoaded', function() {\n";
  html += "  var el = document.querySelectorAll('.tabs'); M.Tabs.init(el, {});\n";
  html += "  var selects = document.querySelectorAll('select'); M.FormSelect.init(selects, {});\n";
  html += "  var modals = document.querySelectorAll('.modal'); M.Modal.init(modals, {});\n";
  html += "  var form = document.getElementById('configform');\n";
  html += "  if(form){ form.onsubmit = function(e){\n";
  html += "    var instance = M.Modal.getInstance(document.getElementById('restartModal'));\n";
  html += "    instance.open(); document.getElementById('restartMsg').innerHTML = '<p>Bitte warten...</p>';\n";
  html += "    document.getElementById('spinner').style.display = '';\n";
  html += "    setTimeout(function(){\n";
  html += "      var start = Date.now();\n";
  html += "      function pollOnline(){\n";
  html += "        fetch('/', {cache:'no-store'}).then(r => {\n";
  html += "          if(r.ok) location.reload(); else setTimeout(pollOnline, 1000);\n";
  html += "        }).catch(() => {\n";
  html += "          if(Date.now()-start < 60000) setTimeout(pollOnline, 1000);\n";
  html += "          else { document.getElementById('spinner').style.display = 'none';\n";
  html += "            document.getElementById('restartMsg').innerHTML = '<div class=\\\"errormsg\\\">Fehler: Server hat sich nicht mehr gemeldet.<br>Bitte aktualisieren.</div>';}\n";
  html += "        });\n";
  html += "      } pollOnline();\n";
  html += "    }, 2000); return true;\n";
  html += "  }}\n";
  html += "});\n";



  // Client list functions
  html += "function statusLabel(val){ if(val==0) return 'allow'; return 'deny'; }\n";
  html += "let editing = {}; let currentList = [];\n";
  html += "function renderTable(list){\n";
  html += "  let body = '';\n";
  html += "  for(let i=0;i<list.length;++i){\n";
  html += "    let c = list[i]; if(!c.callsign) continue;\n";
  html += "    if(editing[i]){\n";
  html += "      body += '<tr><td>'+(i+1)+'</td>';\n";
  html += "      body += '<td><input id=\"e_callsign_'+i+'\" maxlength=\"15\" value=\"'+(c.callsign||\"\")+'\" /></td>';\n";
  html += "      body += '<td><input id=\"e_port_'+i+'\" type=\"number\" min=\"1\" max=\"65535\" value=\"'+(c.port||\"\")+'\" /></td>';\n";
  html += "      body += '<td><select id=\"e_status_'+i+'\">';\n";
  html += "      body += '<option value=\"0\"'+(c.status==0?' selected':'')+'>allow</option>';\n";
  html += "      body += '<option value=\"1\"'+(c.status==1?' selected':'')+'>deny</option></select></td>';\n";
  html += "      body += '<td><button class=\"icon-btn\" onclick=\"saveRow('+i+')\"><i class=\"material-icons green-text\">save</i></button>';\n";
  html += "      body += '<button class=\"icon-btn\" onclick=\"cancelEdit('+i+')\"><i class=\"material-icons\">cancel</i></button>';\n";
  html += "      body += '<button class=\"icon-btn\" onclick=\"delRow('+i+')\"><i class=\"material-icons red-text\">delete</i></button></td></tr>';\n";
  html += "    } else {\n";
  html += "      body += '<tr><td>'+(i+1)+'</td>';\n";
  html += "      body += '<td ondblclick=\"editRow('+i+')\">'+(c.callsign||\"\")+'</td>';\n";
  html += "      body += '<td ondblclick=\"editRow('+i+')\">'+(c.port||\"\")+'</td>';\n";
  html += "      body += '<td ondblclick=\"editRow('+i+')\">'+statusLabel(c.status)+'</td>';\n";
  html += "      body += '<td><button class=\"icon-btn\" onclick=\"editRow('+i+')\"><i class=\"material-icons blue-text\">edit</i></button>';\n";
  html += "      body += '<button class=\"icon-btn\" onclick=\"delRow('+i+')\"><i class=\"material-icons red-text\">delete</i></button></td></tr>';\n";
  html += "    }\n";
  html += "  }\n";
  html += "  document.getElementById('clientbody').innerHTML = body;\n";
  html += "  M.FormSelect.init(document.querySelectorAll('select'));\n";
  html += "}\n";
  html += "function fetchList(){ fetch('/client_list_json').then(r=>r.json()).then(list=>{currentList = list; renderTable(list);}); }\n";
  html += "function editRow(i){ editing[i]=true; renderTable(currentList); }\n";
  html += "function cancelEdit(i){ editing[i]=false; renderTable(currentList); }\n";
  html += "function saveRow(i){\n";
  html += "  let data = { id:i, callsign:document.getElementById('e_callsign_'+i).value,\n";
  html += "    port:document.getElementById('e_port_'+i).value,\n";
  html += "    status:document.getElementById('e_status_'+i).value };\n";
  html += "  fetch('/client_list_save', {method:'POST',body:new URLSearchParams(data)}).then(fetchList);\n";
  html += "  editing[i]=false;\n";
  html += "}\n";
  html += "function delRow(i){ if(!confirm('Wirklich loeschen?')) return; fetch('/client_list_del?id='+i).then(fetchList); }\n";
  html += "function addRow(){\n";
  html += "  let data = { id:-1, callsign:document.getElementById('new_callsign').value,\n";
  html += "    port:document.getElementById('new_port').value,\n";
  html += "    status:document.getElementById('new_status').value };\n";
  html += "  if(!data.callsign || !data.port) { alert('Alle Felder ausfuellen!'); return; }\n";
  html += "  fetch('/client_list_save', {method:'POST',body:new URLSearchParams(data)}).then(_=>{\n";
  html += "    document.getElementById('new_callsign').value=''; \n";
  html += "    document.getElementById('new_port').value=''; document.getElementById('new_status').value='0';\n";
  html += "    fetchList();\n";
  html += "  });\n";
  html += "}\n";
  html += "document.addEventListener('DOMContentLoaded', function(){ fetchList(); M.FormSelect.init(document.querySelectorAll('select')); });\n";
  html += "</script>\n";
  html += "</body></html>";
  
  webserver.send(200, "text/html", html);
}

void handleSave() {
  if (webserver.hasArg("ssid")) strncpy(wifiSsid, webserver.arg("ssid").c_str(), 63);
  if (webserver.hasArg("pass")) strncpy(wifiPass, webserver.arg("pass").c_str(), 63);
  if (webserver.hasArg("callsign")) strncpy(callsign, webserver.arg("callsign").c_str(), 31);
  if (webserver.hasArg("baudrate")) baudrate = webserver.arg("baudrate").toInt();
  if (webserver.hasArg("loglevel")) logLevel = webserver.arg("loglevel").toInt();
  wifiSsid[63]=0; wifiPass[63]=0; callsign[31]=0;
  saveConfig();
  webserver.sendHeader("Location", "/", true);
  webserver.send(302, "text/plain", "");
  delay(500);
  ESP.restart();
}

void startWebserver() {
  webserver.on("/", handleRoot);
  webserver.on("/save", HTTP_POST, handleSave);
  webserver.on("/client_list_json", handleClientListJSON);
  webserver.on("/client_list_save", HTTP_POST, handleClientListSave);
  webserver.on("/client_list_del", handleClientListDelete);
  webserver.on("/monitor", handleMonitor);
  webserver.on("/monitor_clear", handleMonitorClear);
  // OTA-Endpoints:
  webserver.on("/version.txt", HTTP_GET, handleFirmwareVersion);
  webserver.on("/firmware.bin", HTTP_GET, handleFirmwareBin);
  webserver.begin();
}

void startConfigPortal() {
  WiFi.softAP("UDMPRIG-Server");
  IPAddress apip(192,168,4,1);
  WiFi.softAPConfig(apip, apip, IPAddress(255,255,255,0));
  startWebserver();
  bootPrint("AP f. Config gestartet");
  appendMonitor("Access Point für Config gestartet", "INFO");
  while (WiFi.status() != WL_CONNECTED) {
    webserver.handleClient();
    blinkLED();
    delay(1);
  }
  WiFi.softAPdisconnect(true);
  bootPrint("Config-AP beendet");
  appendMonitor("Config-AP beendet", "INFO");
}

bool tryConnectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid, wifiPass);
  bootPrint("Verbinde WLAN ...");
  appendMonitor("WLAN-Verbindung wird aufgebaut...", "INFO");
  for (int i = 0; i < 40; ++i) {
    if (WiFi.status() == WL_CONNECTED) {
      bootPrint("WLAN verbunden ... OK");
      appendMonitor("WLAN verbunden: " + WiFi.localIP().toString(), "INFO");
      return true;
    }
    blinkLED();
    delay(250);
  }
  bootPrint("WLAN fehlgeschlagen!");
  appendMonitor("WLAN Verbindung fehlgeschlagen!", "ERROR");
  return false;
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  Serial.begin(115200);

  display.begin(0x3C, true);
  display.clearDisplay();
  bootPrint("Init Display ... OK");

  EEPROM.begin(EEPROM_SIZE);

  // OTA: LittleFS initialisieren
  if(!LittleFS.begin(true)){
    Serial.println("LittleFS Mount Fehler");
    ax25OledPrint("LittleFS FEHLER!");
    appendMonitor("LittleFS konnte nicht gemountet werden", "ERROR");
    while(1);
  }

  loadConfig();
  bootPrint("Load Config ... OK");

  loadClientList();

  RS232.begin(baudrate, SERIAL_8N1, 16, 17);
  bootPrint("Init RS232 ... OK");
  appendMonitor("RS232 initialisiert mit Baudrate " + String(baudrate), "INFO");

  bool wifiOK = tryConnectWiFi();
  if (wifiOK) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    appendMonitor("NTP angefordert (wird im Hintergrund synchronisiert)", "INFO");
    if (!MDNS.begin("udmprig-server")) {
      bootPrint("mDNS ... FEHLER");
      ax25OledPrint("mDNS FEHLER!");
      appendMonitor("mDNS konnte nicht gestartet werden", "ERROR");
    } else {
      bootPrint("mDNS ... OK");
      appendMonitor("mDNS gestartet als udmprig-server.local", "INFO");
    }
  } else {
    bootPrint("Starte Config-Portal!");
    appendMonitor("WLAN fehlgeschlagen, starte Config-Portal!", "ERROR");
    startConfigPortal();
  }

  bootPrint("Starte Webserver ...");
  startWebserver();
  bootPrint("Webserver online");
  appendMonitor("Webserver online", "INFO");

  server.begin();

  int rssi = WiFi.isConnected() ? WiFi.RSSI() : -100;
  int bars = 0;
  if (rssi > -50) bars = 3;
  else if (rssi > -65) bars = 2;
  else if (rssi > -75) bars = 1;
  else if (rssi > -85) bars = 0;
  drawStatusBarSmallBars(rssi, bars);
  updateMonitorOnly();
}

void printClientStatus() {
  Serial.println("=== CLIENT STATUS ===");
  for (int i = 0; i < MAX_CLIENTS; i++) {
    Serial.printf("Slot %d: %s, Callsign: %s\n", i, 
      (clients[i] && clients[i]->connected()) ? "belegt" : "frei", 
      clientCallsigns[i].c_str());
  }
}

// ============== Multiplexer-Loop inkl. AX.25 ==============
void loop() {
  webserver.handleClient();

  // --- NEUE VERBINDUNGEN ---
  WiFiClient newClient = server.available();
  if (newClient) {
    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (!clients[i] || !clients[i]->connected()) { slot = i; break; }
    }
    if (slot != -1) {
      if (clients[slot]) {
        clients[slot]->stop();
        delete clients[slot];
        clients[slot] = nullptr;
      }
      clients[slot] = new WiFiClient(newClient);
      clients[slot]->setNoDelay(true);
      String callTry = "";
      unsigned long start = millis();
      while (callTry.length() < 16 && millis() - start < 2000) {
        while (clients[slot]->available()) {
          char ch = clients[slot]->read();
          if (ch == '\n' || ch == '\r') goto call_done;
          callTry += ch;
        }
        delay(1);
      }
      call_done:
      callTry.trim();
      if (callTry.length() == 0 || !isCallsignAllowed(callTry)) {
        appendMonitor("Verbindung abgelehnt für Callsign: " + callTry, "WARNING");
        ax25OledPrint("Verbindung abgelehnt: " + callTry);
        clients[slot]->print("DENY\n");
        clients[slot]->stop();
        delete clients[slot];
        clients[slot] = nullptr;
      } else {
        clients[slot]->print("OK\n");
        clientCallsigns[slot] = callTry;
        clientLastActivity[slot] = millis();
        appendMonitor("Client verbunden: " + callTry, "INFO");
        ax25OledPrint("Client verbunden: " + callTry);
        printClientStatus();
      }
    } else {
      newClient.stop();
    }
  }

  // --- RS232 -> ALLE CLIENTS & AX25-Monitor ---
  uint8_t buf[256];
  int n = RS232.available();
  while (n > 0) {
    int readLen = n > (int)sizeof(buf) ? sizeof(buf) : n;
    int actual = RS232.readBytes(buf, readLen);
    for (int i = 0; i < MAX_CLIENTS; i++)
      if (clients[i] && clients[i]->connected())
        clients[i]->write(buf, actual);
    for (int k = 0; k < actual; ++k)
      if (txcnt < RX_BUF_SIZE) txbuf[txcnt++] = buf[k];
    n -= actual;
  }

  // --- AX25-Parsing für gesendete Frames (RS232->Clients) ---
  size_t txstart = 0, txend = 0;
  while (txstart < txcnt) {
    while (txstart < txcnt && txbuf[txstart] != 0xC0) txstart++;
    if (txstart >= txcnt) break;
    txend = txstart + 1;
    while (txend < txcnt && txbuf[txend] != 0xC0) txend++;
    if (txend >= txcnt) break;
    if (txend - txstart > 2) {
      uint8_t kiss_payload[330];
      size_t kiss_len = txend - txstart - 1;
      memcpy(kiss_payload, txbuf + txstart + 1, kiss_len);
      if (kiss_len >= 2) {
        uint8_t kiss_cmd = kiss_payload[0];
        size_t ax25_len = kiss_len - 1;
        uint8_t ax25_frame[330];
        size_t ax25_real_len = kiss_unescape(kiss_payload + 1, ax25_len, ax25_frame);
        printAX25Packet(ax25_frame, ax25_real_len, false);
      }
    }
    txstart = txend;
  }
  if (txstart >= txcnt) txcnt = 0;
  else if (txstart > 0) {
    memmove(txbuf, txbuf + txstart, txcnt - txstart);
    txcnt -= txstart;
  }

  // --- CLIENTS -> RS232 + andere Clients & AX25-Monitor ---
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (!clients[i] || !clients[i]->connected()) continue;
    int availableBytes = clients[i]->available();
    while (availableBytes > 0) {
      int len = availableBytes > (int)sizeof(buf) ? sizeof(buf) : availableBytes;
      int readLen = clients[i]->readBytes(buf, len);
      if (readLen > 0) {
        RS232.write(buf, readLen);
        for (int j = 0; j < MAX_CLIENTS; j++)
          if (j != i && clients[j] && clients[j]->connected())
            clients[j]->write(buf, readLen);
        clientLastActivity[i] = millis();
        for (int k = 0; k < readLen; ++k)
          if (rxcnt < RX_BUF_SIZE) rxbuf[rxcnt++] = buf[k];
      }
      availableBytes -= readLen;
    }
  }

  // --- AX25-Parsing für empfangene Frames (Clients->RS232) ---
  size_t start = 0, end = 0;
  while (start < rxcnt) {
    while (start < rxcnt && rxbuf[start] != 0xC0) start++;
    if (start >= rxcnt) break;
    end = start + 1;
    while (end < rxcnt && rxbuf[end] != 0xC0) end++;
    if (end >= rxcnt) break;
    if (end - start > 2) {
      uint8_t kiss_payload[330];
      size_t kiss_len = end - start - 1;
      memcpy(kiss_payload, rxbuf + start + 1, kiss_len);
      if (kiss_len >= 2) {
        uint8_t kiss_cmd = kiss_payload[0];
        size_t ax25_len = kiss_len - 1;
        uint8_t ax25_frame[330];
        size_t ax25_real_len = kiss_unescape(kiss_payload + 1, ax25_len, ax25_frame);
        printAX25Packet(ax25_frame, ax25_real_len, true);
      }
    }
    start = end;
  }
  if (start >= rxcnt) rxcnt = 0;
  else if (start > 0) {
    memmove(rxbuf, rxbuf + start, rxcnt - start);
    rxcnt -= start;
  }

  // --- TIMEOUTS ---
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i] && clients[i]->connected()) {
      if (millis() - clientLastActivity[i] > clientTimeoutMs) {
        appendMonitor("Client Timeout: " + clientCallsigns[i], "WARNING");
        ax25OledPrint("Client Timeout: " + clientCallsigns[i]);
        clients[i]->stop();
        delete clients[i];
        clients[i] = nullptr;
        clientCallsigns[i] = "";
        printClientStatus();
      }
    }
  }

  // --- OLED/Monitor-Update regelmäßig ---
  if (millis() - lastStatusUpdate > 333) {
    int rssi = WiFi.isConnected() ? WiFi.RSSI() : -100;
    int bars = 0;
    if (rssi > -50) bars = 3;
    else if (rssi > -65) bars = 2;
    else if (rssi > -75) bars = 1;
    else if (rssi > -85) bars = 0;
    drawStatusBarSmallBars(rssi, bars);
    display.display();
    lastStatusUpdate = millis();
  }
  if (monitorDirty && millis() - lastMonitorUpdate > 30) {
    updateMonitorOnly();
    lastMonitorUpdate = millis();
    monitorDirty = false;
  }

  blinkLED();
}

void blinkLED() {
  static unsigned long lastBlink = 0;
  static bool ledState = false;
  unsigned long now = millis();
  if (now - lastBlink > 250) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    lastBlink = now;
  }
}
