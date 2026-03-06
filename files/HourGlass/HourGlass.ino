// HourGlass.ino — ESP32 Version with Full Color & Time Format Customization
#include "Adafruit_GFX.h"
#include "Adafruit_GC9A01A.h"
#include "colors.h"
#include <EEPROM.h>
#include <RTClib.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

// ===== TIMER MODE =====
bool HOUR_MODE = false;

// ===== PIN DEFINITIONS (ESP32 SAFE PINS) =====
#define TFT1_DC            5
#define TFT1_CS            2
#define TFT1_RST           4

#define TFT2_DC            17
#define TFT2_CS            19
#define TFT2_RST           15

#define RTC_SET_FLAG_ADDR  0
#define NTP_SYNC_TIME_ADDR 1
#define CONFIG_VALID_ADDR  6

// EEPROM layout:
//   100     : HOUR_MODE (0 or 1)
//   101-164 : NTP server string (64 bytes)
//   165-168 : Timezone offset in minutes (int32_t)
//   169     : Clock Color Index (uint8_t)
//   170     : Background Color Index (uint8_t)
//   171     : Sand Color Index (uint8_t)
//   172     : 24-Hour Mode (0=12hr, 1=24hr) (uint8_t)

// ===== STREAM GEOMETRY CONSTANTS =====
#define STREAM_CX              120
#define STREAM_X_HALF            7
#define STREAM_X_MIN           (STREAM_CX - STREAM_X_HALF)        // 113
#define STREAM_X_MAX           (STREAM_CX + STREAM_X_HALF)        // 127
#define STREAM_CLEAR_MARGIN      3
#define STREAM_CLEAR_X         (STREAM_X_MIN - STREAM_CLEAR_MARGIN)              // 110
#define STREAM_CLEAR_W         (STREAM_X_MAX - STREAM_X_MIN + 1 + 2 * STREAM_CLEAR_MARGIN) // 21

// ===== DYNAMIC COLOR VARIABLES =====
uint16_t clockColor;
uint16_t bgColor;
uint16_t sandColor;

// ===== DISPLAY OBJECTS =====
Adafruit_GC9A01A tft1(TFT1_CS, TFT1_DC, TFT1_RST);   // Upper glass
Adafruit_GC9A01A tft2(TFT2_CS, TFT2_DC, TFT2_RST);   // Lower glass

// ===== RTC =====
RTC_DS3231 rtc;

// ===== WiFi / NTP / Web server =====
WiFiUDP          ntpUDP;
NTPClient        timeClient(ntpUDP, "time.google.com", 0, 60000);
WebServer        server(80);
bool             ntpSynced    = false;
unsigned long    lastSyncTime = 0;
const unsigned long syncInterval = 86400000UL;

// ===== CONFIGURATION =====
bool configHourMode      = false;
char configNtpServer[64] = "time.google.com";
int32_t configTimezoneOffset = -300; // Default to US Eastern (UTC-5, or -300 minutes)
uint8_t configClockColorIndex = 6;    // Default to Cyan
uint8_t configBgColorIndex    = 0;    // Default to Black
uint8_t configSandColorIndex  = 12;   // Default to Sand
bool configIs24Hour = false;          // Default to 12-hour format

// ===== ANIMATION STATE =====
const int     totalFrames          = 141;
int           m                    = 0;
int           n                    = 0;
unsigned long animationStartMillis = 0;
int           lastMinuteTracked    = -1;
bool          needsFullRedraw      = true;
bool          needsTimeUpdate      = false;
unsigned long lastStreamUpdate     = 0;
int           streamOffset         = 0;

// ===== CURRENT LOCAL TIME =====
int currentMinute = 0;
int currentHour   = 0;
int hour12        = 0;

// ===== WiFiManager portal-save flag =====
bool portalConfigSaved = false;
void onPortalSave() { portalConfigSaved = true; }

// ===== HELPER FUNCTION =====
String getDeviceId() {
  uint64_t chipid = ESP.getEfuseMac();
  uint32_t high = chipid >> 32;
  uint32_t low  = chipid & 0xFFFFFFFF;
  char id[12];
  snprintf(id, sizeof(id), "%04X%08X", high, low);
  return String(id);
}

// ===== FORWARD DECLARATIONS =====
void drawFrame(int frame);
void drawSandStream(int bottomY);
void drawTimeDisplay();
void saveConfig();
void loadConfig();
void setupWiFi();
void startWebServer();
void syncRTCwithNTP();
void applyColors();

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(2000);

  EEPROM.begin(512);
  loadConfig();
  applyColors(); // Apply loaded colors to the dynamic variables

  tft1.begin();   tft2.begin();
  tft1.setRotation(2);   tft2.setRotation(0);

  tft1.fillScreen(bgColor);
  tft2.fillScreen(bgColor);

  if (!rtc.begin()) {
    Serial.println("RTC not found!");
    while (1) delay(10);
  }

  bool isFirstRun = (EEPROM.read(RTC_SET_FLAG_ADDR) != 1);

  setupWiFi();
  syncRTCwithNTP();

  if (isFirstRun && !ntpSynced) {
    Serial.println("NTP failed — using compile timestamp.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  if (isFirstRun) {
    EEPROM.write(RTC_SET_FLAG_ADDR, 1);
    EEPROM.commit();
  }

  randomSeed(analogRead(A0));

  time_t utc = rtc.now().unixtime();
  time_t local = utc + (configTimezoneOffset * 60);
  DateTime localTime(local);

  currentMinute = localTime.minute();
  hour12        = localTime.hour();
  currentHour   = (hour12 == 0) ? 12 : ((hour12 > 12) ? hour12 - 12 : hour12);
  lastMinuteTracked = currentMinute;

  int secondsIn = HOUR_MODE
    ? (localTime.minute() * 60 + localTime.second())
    : localTime.second();
  animationStartMillis = millis() - (secondsIn * 1000UL);

  Serial.printf("Local: %02d:%02d:%02d  Mode: %s  Offset: %d min  Format: %s\n",
    localTime.hour(), localTime.minute(), localTime.second(),
    HOUR_MODE ? "HOUR" : "MIN", configTimezoneOffset,
    configIs24Hour ? "24hr" : "12hr");
}

// ============================================================
//  WiFi SETUP
// ============================================================
void setupWiFi() {
  portalConfigSaved = false;
  WiFi.mode(WIFI_STA);

  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(onPortalSave);
  wifiManager.setConfigPortalTimeout(180);

  char modeStr[2]    = { configHourMode ? '1' : '0', '\0' };
  char offsetStr[8]; snprintf(offsetStr, sizeof(offsetStr), "%d", configTimezoneOffset);
  char clockStr[3];  snprintf(clockStr, sizeof(clockStr), "%d", configClockColorIndex);
  char bgStr[3];     snprintf(bgStr, sizeof(bgStr), "%d", configBgColorIndex);
  char sandStr[3];   snprintf(sandStr, sizeof(sandStr), "%d", configSandColorIndex);
  char formatStr[2]; snprintf(formatStr, sizeof(formatStr), "%d", configIs24Hour ? 1 : 0);

  WiFiManagerParameter paramNtp    ("ntp_server",   "NTP Server",                    configNtpServer, 64);
  WiFiManagerParameter paramMode   ("hour_mode",    "Timer Mode (0=Min, 1=Hr)",      modeStr,         2);
  WiFiManagerParameter paramOffset ("tz_offset",    "TZ Offset (min from UTC)",      offsetStr,      7);
  WiFiManagerParameter paramClock  ("clock_color",  "Clock Color Index",              clockStr,        2);
  WiFiManagerParameter paramBg     ("bg_color",     "Background Color Index",         bgStr,           2);
  WiFiManagerParameter paramSand   ("sand_color",   "Sand Color Index",               sandStr,         2);
  WiFiManagerParameter paramFormat ("time_format",  "Time Format (0=12hr, 1=24hr)",  formatStr,       2);

  wifiManager.addParameter(&paramNtp);
  wifiManager.addParameter(&paramMode);
  wifiManager.addParameter(&paramOffset);
  wifiManager.addParameter(&paramClock);
  wifiManager.addParameter(&paramBg);
  wifiManager.addParameter(&paramSand);
  wifiManager.addParameter(&paramFormat);

  tft1.fillRect(0, 55, 240, 50, bgColor);
  tft1.setTextColor(clockColor);
  tft1.setTextSize(2);
  tft1.setCursor(10, 65);
  tft1.print("WiFi...");
  tft1.setTextSize(1);
  tft1.setCursor(10, 90);
  tft1.print("SSID: Hourglass-");
  tft1.setCursor(10, 100);
  tft1.print(getDeviceId());

  if (!wifiManager.autoConnect(("Hourglass-" + getDeviceId()).c_str())) {
    Serial.println("WiFi failed.");
    tft1.fillRect(0, 55, 240, 50, bgColor);
    return;
  }

  Serial.print("WiFi OK. IP: ");
  Serial.println(WiFi.localIP());

  if (portalConfigSaved) {
    Serial.println("Portal submitted — updating config.");
    const char* ntpVal  = paramNtp.getValue();
    const char* modeVal = paramMode.getValue();
    const char* offsetVal = paramOffset.getValue();
    const char* clockVal = paramClock.getValue();
    const char* bgVal = paramBg.getValue();
    const char* sandVal = paramSand.getValue();
    const char* formatVal = paramFormat.getValue();

    if (strlen(ntpVal) > 0) strncpy(configNtpServer, ntpVal, 63);
    if (strlen(modeVal) > 0) configHourMode = (strcmp(modeVal, "1") == 0);
    if (strlen(offsetVal) > 0) configTimezoneOffset = atoi(offsetVal);
    if (strlen(clockVal) > 0) configClockColorIndex = atoi(clockVal);
    if (strlen(bgVal) > 0) configBgColorIndex = atoi(bgVal);
    if (strlen(sandVal) > 0) configSandColorIndex = atoi(sandVal);
    if (strlen(formatVal) > 0) configIs24Hour = (strcmp(formatVal, "1") == 0);
    
    saveConfig();
    applyColors(); // Apply new colors
  } else {
    Serial.println("Auto-connected — EEPROM config unchanged.");
  }

  startWebServer();
  tft1.fillRect(0, 55, 240, 50, bgColor);
}

// ============================================================
//  WEB SERVER
// ============================================================
void startWebServer() {
  server.on("/", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head><title>Hourglass</title>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>body{font-family:sans-serif;padding:20px;background:#f0f0f0;}";
    html += "h1{text-align:center;}";
    html += ".card{background:#fff;padding:15px;border-radius:8px;margin:10px 0;}";
    html += "a.btn{display:block;text-align:center;padding:14px;background:#2196F3;";
    html += "color:#fff;text-decoration:none;border-radius:6px;margin-top:12px;font-size:16px;}";
    html += "</style></head><body>";
    html += "<h1>&#8987; Hourglass</h1><div class='card'>";
    html += "<p><b>Mode:</b> " + String(HOUR_MODE ? "Hour (1 hr/cycle)" : "Minute (1 min/cycle)") + "</p>";
    html += "<p><b>NTP:</b> "  + String(configNtpServer) + "</p>";
    html += "<p><b>Offset:</b> " + String(configTimezoneOffset) + " min from UTC</p>";
    html += "<p><b>Format:</b> " + String(configIs24Hour ? "24-Hour" : "12-Hour") + "</p>";
    html += "<p><b>Clock:</b> " + String(colorNames[configClockColorIndex]) + "</p>";
    html += "<p><b>Background:</b> " + String(colorNames[configBgColorIndex]) + "</p>";
    html += "<p><b>Sand:</b> " + String(colorNames[configSandColorIndex]) + "</p>";
    html += "<p><b>IP:</b> "   + WiFi.localIP().toString() + "</p>";
    html += "</div><a class='btn' href='/config'>&#9881; Configure</a></body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/config", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head><title>Configure</title>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>body{font-family:sans-serif;padding:20px;background:#f0f0f0;}";
    html += "h1{text-align:center;}";
    html += "label{display:block;margin:14px 0 4px;font-weight:bold;}";
    html += "input,select{width:100%;padding:10px;box-sizing:border-box;";
    html += "border:1px solid #ccc;border-radius:4px;font-size:15px;}";
    html += "button{width:100%;padding:14px;background:#4CAF50;color:#fff;border:none;";
    html += "border-radius:6px;font-size:16px;margin-top:18px;cursor:pointer;}";
    html += "button:hover{background:#388E3C;}</style></head><body>";
    html += "<h1>&#9881; Configure</h1><form action='/save' method='POST'>";

    html += "<label>Timer Mode</label><select name='mode'>";
    html += (HOUR_MODE ? "<option value='hour' selected>Hour Mode (1 hr/cycle)</option><option value='minute'>Minute Mode (1 min/cycle)</option>" : "<option value='minute' selected>Minute Mode (1 min/cycle)</option><option value='hour'>Hour Mode (1 hr/cycle)</option>");
    html += "</select>";

    html += "<label>Time Format</label><select name='format'>";
    html += (configIs24Hour ? "<option value='24' selected>24-Hour Format</option><option value='12'>12-Hour Format</option>" : "<option value='12' selected>12-Hour Format</option><option value='24'>24-Hour Format</option>");
    html += "</select>";

    html += "<label>NTP Server</label>";
    html += "<input type='text' name='ntp' value='" + String(configNtpServer) + "' maxlength='63'>";

    html += "<label>Timezone</label><select name='offset'>";
    struct TzOption { const char* name; int offset; };
    TzOption options[] = { {"UTC", 0}, {"GMT (London)", 0}, {"EST (New York)", -300}, {"CST (Chicago)", -360}, {"MST (Denver)", -420}, {"PST (Los Angeles)", -480}, {"CET (Paris)", 60}, {"IST (India)", 330}, {"CST (Beijing)", 480}, {"JST (Tokyo)", 540} };
    for (auto& opt : options) {
      html += "<option value='" + String(opt.offset) + "'" + (configTimezoneOffset == opt.offset ? " selected" : "") + ">" + String(opt.name) + "</option>";
    }
    html += "</select>";

    html += "<label>Clock Color</label><select name='clock'>";
    for(int i=0; i<COLOR_PALETTE_COUNT; i++) {
      html += "<option value='" + String(i) + "'" + (configClockColorIndex == i ? " selected" : "") + ">" + String(colorNames[i]) + "</option>";
    }
    html += "</select>";

    html += "<label>Background Color</label><select name='bg'>";
    for(int i=0; i<COLOR_PALETTE_COUNT; i++) {
      html += "<option value='" + String(i) + "'" + (configBgColorIndex == i ? " selected" : "") + ">" + String(colorNames[i]) + "</option>";
    }
    html += "</select>";

    html += "<label>Sand Color</label><select name='sand'>";
    for(int i=0; i<COLOR_PALETTE_COUNT; i++) {
      html += "<option value='" + String(i) + "'" + (configSandColorIndex == i ? " selected" : "") + ">" + String(colorNames[i]) + "</option>";
    }
    html += "</select>";

    html += "<button type='submit'>&#128190; Save &amp; Restart</button></form>";
    html += "<p style='text-align:center;color:#999;margin-top:14px;'>IP: ";
    html += WiFi.localIP().toString() + "</p></body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/save", HTTP_POST, []() {
    configHourMode = (server.arg("mode") == "hour");
    configIs24Hour = (server.arg("format") == "24");
    if (server.arg("ntp").length() > 0) strncpy(configNtpServer, server.arg("ntp").c_str(), 63);
    configTimezoneOffset = server.arg("offset").toInt();
    configClockColorIndex = server.arg("clock").toInt();
    configBgColorIndex = server.arg("bg").toInt();
    configSandColorIndex = server.arg("sand").toInt();
    HOUR_MODE = configHourMode;

    saveConfig();
    applyColors();

    String html = "<!DOCTYPE html><html><head><title>Saved</title>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<meta http-equiv='refresh' content='5;url=/'>";
    html += "<style>body{font-family:sans-serif;padding:30px;text-align:center;background:#f0f0f0;}";
    html += "h1{color:#4CAF50;}</style></head><body><h1>&#10003; Saved!</h1>";
    html += "<p>Restarting in 3 seconds&hellip;</p></body></html>";
    server.send(200, "text/html", html);
    delay(3000);
    ESP.restart();
  });

  server.begin();
  Serial.printf("Web server at http://%s/\n", WiFi.localIP().toString().c_str());
}

// ============================================================
//  EEPROM
// ============================================================
void saveConfig() {
  EEPROM.write(100, HOUR_MODE ? 1 : 0);
  for (int i = 0; i < 64; i++) EEPROM.write(101 + i, (uint8_t)configNtpServer[i]);
  EEPROM.put(165, configTimezoneOffset);
  EEPROM.write(169, configClockColorIndex);
  EEPROM.write(170, configBgColorIndex);
  EEPROM.write(171, configSandColorIndex);
  EEPROM.write(172, configIs24Hour ? 1 : 0);
  EEPROM.write(CONFIG_VALID_ADDR, 0xAB);
  EEPROM.commit();
  Serial.printf("saveConfig: mode=%d, ntp=%s, offset=%d, format=%s, clock=%d, bg=%d, sand=%d\n",
    HOUR_MODE ? 1 : 0, configNtpServer, configTimezoneOffset, configIs24Hour ? "24hr" : "12hr", configClockColorIndex, configBgColorIndex, configSandColorIndex);
}

void loadConfig() {
  if (EEPROM.read(CONFIG_VALID_ADDR) != 0xAB) {
    Serial.println("No saved config — using defaults.");
    return;
  }
  configHourMode = (EEPROM.read(100) == 1);
  HOUR_MODE = configHourMode;
  for (int i = 0; i < 64; i++) configNtpServer[i] = (char)EEPROM.read(101 + i);
  configNtpServer[63] = '\0';
  if (configNtpServer[0] == '\0' || (uint8_t)configNtpServer[0] == 0xFF) strncpy(configNtpServer, "time.google.com", 63);
  EEPROM.get(165, configTimezoneOffset);
  if (configTimezoneOffset < -720 || configTimezoneOffset > 840) configTimezoneOffset = -300;
  configClockColorIndex = EEPROM.read(169);
  configBgColorIndex = EEPROM.read(170);
  configSandColorIndex = EEPROM.read(171);
  configIs24Hour = (EEPROM.read(172) == 1);
  if (configClockColorIndex >= COLOR_PALETTE_COUNT) configClockColorIndex = 6;
  if (configBgColorIndex >= COLOR_PALETTE_COUNT) configBgColorIndex = 0;
  if (configSandColorIndex >= COLOR_PALETTE_COUNT) configSandColorIndex = 12;

  Serial.printf("loadConfig: mode=%s, ntp=%s, offset=%d, format=%s, clock=%d, bg=%d, sand=%d\n",
    HOUR_MODE ? "hour" : "minute", configNtpServer, configTimezoneOffset, configIs24Hour ? "24hr" : "12hr", configClockColorIndex, configBgColorIndex, configSandColorIndex);
}

// ============================================================
//  APPLY COLORS
// ============================================================
void applyColors() {
  clockColor = colorPalette[configClockColorIndex];
  bgColor = colorPalette[configBgColorIndex];
  sandColor = colorPalette[configSandColorIndex];
  Serial.printf("Applying colors: Clock=%s, BG=%s, Sand=%s\n",
    colorNames[configClockColorIndex], colorNames[configBgColorIndex], colorNames[configSandColorIndex]);
}

// ============================================================
//  NTP SYNC
// ============================================================
void syncRTCwithNTP() {
  if (WiFi.status() != WL_CONNECTED) { Serial.println("No WiFi — NTP skipped."); return; }
  Serial.println("Syncing NTP...");
  timeClient.begin(); timeClient.setPoolServerName(configNtpServer);
  int attempts = 0; while (!timeClient.update() && attempts < 10) { delay(500); attempts++; yield(); }
  if (timeClient.isTimeSet()) {
    unsigned long epoch = timeClient.getEpochTime();
    rtc.adjust(DateTime(epoch)); ntpSynced = true; lastSyncTime = millis();
    EEPROM.put(NTP_SYNC_TIME_ADDR, lastSyncTime); EEPROM.commit();
    time_t local = epoch + (configTimezoneOffset * 60); DateTime ld(local);
    Serial.printf("NTP OK — Local: %02d:%02d:%02d  Offset: %d min\n", ld.hour(), ld.minute(), ld.second(), configTimezoneOffset);
  } else { Serial.println("NTP failed."); ntpSynced = false; }
}

// ============================================================
//  MAIN LOOP
// ============================================================
void loop() {
  server.handleClient();
  time_t utc = rtc.now().unixtime();
  time_t local = utc + (configTimezoneOffset * 60);
  DateTime now(local);
  currentMinute = now.minute(); hour12 = now.hour();
  currentHour = (hour12 == 0) ? 12 : ((hour12 > 12) ? hour12 - 12 : hour12);
  if (currentMinute != lastMinuteTracked) {
    lastMinuteTracked = currentMinute;
    Serial.printf("Tick: %02d:%02d  mode=%s\n", currentHour, currentMinute, HOUR_MODE ? "HOUR" : "MIN");
    if (!HOUR_MODE) { animationStartMillis = millis(); needsFullRedraw = true; } else { needsTimeUpdate = true; }
  }
  unsigned long elapsed = millis() - animationStartMillis;
  unsigned long intervalDuration = HOUR_MODE ? 3600000UL : 60000UL;
  if (HOUR_MODE && elapsed >= intervalDuration) {
    animationStartMillis = millis(); elapsed = 0; needsFullRedraw = true; Serial.println("Hour complete — resetting animation.");
  }
  int currentFrame = (int)constrain((long)(elapsed / (intervalDuration / totalFrames)), 0L, (long)(totalFrames - 1));
  if (millis() - lastStreamUpdate >= (HOUR_MODE ? 400UL : 150UL)) { lastStreamUpdate = millis(); streamOffset = (streamOffset + 12) % 240; }
  drawFrame(currentFrame);
  if (ntpSynced && millis() - lastSyncTime > syncInterval) {
    Serial.println("24h elapsed — re-syncing NTP...");
    if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); int w = 0; while (WiFi.status() != WL_CONNECTED && w < 20) { delay(500); w++; yield(); } }
    syncRTCwithNTP();
    time_t su = rtc.now().unixtime(); time_t sl = su + (configTimezoneOffset * 60); DateTime slocal(sl);
    int secs = HOUR_MODE ? (slocal.minute() * 60 + slocal.second()) : slocal.second();
    animationStartMillis = millis() - (secs * 1000UL); needsFullRedraw = true;
  }
  yield(); delay(50);
}

// ============================================================
//  DRAW TIME DIGITS ONLY
// ============================================================
void drawTimeDisplay() {
  tft1.fillRect(55, 0, 185, 55, bgColor);
  tft1.setCursor(60, 10);
  tft1.setTextColor(clockColor);
  tft1.setTextSize(4);

  // Use the correct hour based on the 12/24-hour setting
  int displayHour = configIs24Hour ? hour12 : currentHour;

  if (displayHour < 10) tft1.print("0");
  tft1.print(displayHour);
  tft1.print(":");
  if (currentMinute < 10) tft1.print("0");
  tft1.print(currentMinute);
  needsTimeUpdate = false;
}

// ============================================================
//  DRAW FRAME
// ============================================================
void drawFrame(int frame) {
  static int lastFrame = -1;
  bool doFullRedraw = needsFullRedraw || (lastFrame == -1) || (abs(frame - lastFrame) > 5);
  if (doFullRedraw) {
    tft1.fillScreen(bgColor); tft2.fillScreen(bgColor); drawTimeDisplay();
    if (frame > 0) {
      m = frame; int upperY = (int)(m * 0.9f + 68); 
      int upperTip = (int)(m * 1.2f + 90);
      tft1.fillRect(0, upperY, 240, 240 - upperY, sandColor);
      tft1.fillTriangle(0, upperY, 240, upperY, 120, upperTip, bgColor);
      n = map(m, 0, totalFrames, 250, 75); 
      int lowerY = (int)(n * 0.55f + 105);
      if (lowerY < 240) { 
        tft2.fillTriangle(0, lowerY - 1, 240, lowerY - 1, 120, n, sandColor); 
        tft2.fillRect(0, lowerY - 1, 240, 241 - lowerY, sandColor); 
      }
      drawSandStream(n);
    } else { tft1.fillRect(0, 100, 240, 140, sandColor); }
    needsFullRedraw = false; lastFrame = frame; yield(); return;
  }
  if (needsTimeUpdate) drawTimeDisplay();
  if (frame == lastFrame) { 
    if (m > 0 && m < totalFrames - 1) drawSandStream(n); return; 
  }
  m = frame; int upperY = (int)(m * 0.9f + 68); 
  int upperTip = (int)(m * 1.2f + 90);
  tft1.fillTriangle(0, upperY, 240, upperY, 120, upperTip, bgColor);
  n = map(m, 0, totalFrames, 250, 75); 
  int lowerY = (int)(n * 0.55f + 105);
  if (lowerY < 240) { 
    tft2.fillTriangle(0, lowerY - 1, 240, lowerY - 1, 120, n, sandColor); 
    tft2.fillRect(0, lowerY - 1, 240, 241 - lowerY, sandColor);
  }
  if (m < totalFrames - 1) drawSandStream(n);
  lastFrame = frame; yield();
}

// ============================================================
//  DRAW SAND STREAM
// ============================================================
void drawSandStream(int bottomY) {
  int streamBottom = min(bottomY - 10, 240); if (streamBottom <= 0) return;
  tft2.fillRect(STREAM_CLEAR_X, 0, STREAM_CLEAR_W, streamBottom, bgColor);
  for (int i = 0; i < streamBottom; i += 8) {
    int pos = (i + streamOffset) % streamBottom; if (pos < 0 || pos >= streamBottom) continue;
    int xOff = constrain((int)random(-4, 5), -(STREAM_X_HALF - 2), (STREAM_X_HALF - 2));
    int radius = (int)random(1, 3); int cx = STREAM_CX + xOff;
    if (cx - radius >= STREAM_CLEAR_X && cx + radius <  STREAM_CLEAR_X + STREAM_CLEAR_W) { tft2.fillCircle(cx, pos, radius, sandColor); }
    if (random(3) == 0) {
      int cx2 = constrain(cx + (int)random(-2, 3), STREAM_CLEAR_X + 1, STREAM_CLEAR_X + STREAM_CLEAR_W - 2);
      int cy2 = pos + (int)random(-3, 4); if (cy2 >= 0 && cy2 < streamBottom) tft2.fillCircle(cx2, cy2, 1, sandColor);
    }
  }
  for (int i = 0; i < 5; i++) {
    int gy = bottomY + (int)random(-12, -2); int gx = STREAM_CX + (int)random(-5, 6); int gr = (int)random(1, 3);
    if (gy > 0 && gy < 240 && gx - gr >= STREAM_CLEAR_X && gx + gr <  STREAM_CLEAR_X + STREAM_CLEAR_W) { tft2.fillCircle(gx, gy, gr, sandColor); }
  }
  for (int i = 0; i < 3; i++) {
    int dx = STREAM_CX + (int)random(-5, 6); int dy = (int)random(0, streamBottom);
    if (dx >= STREAM_CLEAR_X && dx < STREAM_CLEAR_X + STREAM_CLEAR_W) tft2.drawPixel(dx, dy, sandColor);
  }
}