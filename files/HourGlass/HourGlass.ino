// HourGlass.ino — ESP32 with DST, Extended Timezones, Factory/WiFi Reset
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

// ===== PIN DEFINITIONS =====
#define TFT1_DC   5
#define TFT1_CS   2
#define TFT1_RST  4
#define TFT2_DC   17
#define TFT2_CS   19
#define TFT2_RST  15

#define RTC_SET_FLAG_ADDR  0
#define NTP_SYNC_TIME_ADDR 1
#define CONFIG_VALID_ADDR  6
#define CONFIG_VALID_BYTE  0xAC  // Changed from 0xAB — forces clean reset on first load

// EEPROM layout:
//   100     : HOUR_MODE (0 or 1)
//   101-164 : NTP server string (64 bytes)
//   165     : Timezone index (uint8_t)   <-- replaces old int32_t offset
//   166     : DST enabled (uint8_t)      <-- NEW
//   167-168 : reserved
//   169     : Clock Color Index (uint8_t)
//   170     : Background Color Index (uint8_t)
//   171     : Sand Color Index (uint8_t)
//   172     : 24-Hour Mode (uint8_t)

// ===== STREAM GEOMETRY CONSTANTS =====
#define STREAM_CX           120
#define STREAM_X_HALF         7
#define STREAM_X_MIN        (STREAM_CX - STREAM_X_HALF)
#define STREAM_X_MAX        (STREAM_CX + STREAM_X_HALF)
#define STREAM_CLEAR_MARGIN   3
#define STREAM_CLEAR_X      (STREAM_X_MIN - STREAM_CLEAR_MARGIN)
#define STREAM_CLEAR_W      (STREAM_X_MAX - STREAM_X_MIN + 1 + 2 * STREAM_CLEAR_MARGIN)

// ============================================================
//  TIMEZONE TABLE
//  DST Rules:
//    0 = No DST
//    1 = US/Canada  (2nd Sun Mar 2AM  — 1st Sun Nov 2AM)
//    2 = EU/UK      (last Sun Mar 2AM — last Sun Oct 2AM)
//    3 = S.Hemisphere (1st Sun Oct 2AM — 1st Sun Apr 2AM)
// ============================================================
struct TimezoneEntry {
  const char* name;
  int standardOffset; // minutes from UTC
  int dstOffset;      // additional minutes during DST (usually 60)
  int dstRule;
};

const TimezoneEntry timezones[] = {
  {"(UTC-12:00) Baker Island",              -720,  0, 0},
  {"(UTC-11:00) Pago Pago (SST)",           -660,  0, 0},
  {"(UTC-10:00) Honolulu (HST)",            -600,  0, 0},
  {"(UTC-09:00) Anchorage (AKST)",          -540, 60, 1},
  {"(UTC-08:00) Los Angeles (PST)",         -480, 60, 1},
  {"(UTC-07:00) Denver (MST)",              -420, 60, 1},
  {"(UTC-07:00) Phoenix (MST / no DST)",    -420,  0, 0},
  {"(UTC-06:00) Chicago (CST)",             -360, 60, 1},
  {"(UTC-05:00) New York (EST)",            -300, 60, 1},  // index 8 = DEFAULT
  {"(UTC-04:00) Halifax (AST)",             -240, 60, 1},
  {"(UTC-03:30) St. Johns (NST)",           -210, 60, 1},
  {"(UTC-03:00) Sao Paulo (BRT)",           -180,  0, 0},
  {"(UTC-03:00) Buenos Aires (ART)",        -180,  0, 0},
  {"(UTC-02:00) South Georgia",             -120,  0, 0},
  {"(UTC-01:00) Azores (AZOT)",              -60, 60, 2},
  {"(UTC+00:00) London (GMT)",                 0, 60, 2},
  {"(UTC+00:00) Reykjavik (GMT/no DST)",       0,  0, 0},
  {"(UTC+01:00) Paris / Berlin (CET)",        60, 60, 2},
  {"(UTC+01:00) Lagos (WAT/no DST)",          60,  0, 0},
  {"(UTC+02:00) Athens / Helsinki (EET)",    120, 60, 2},
  {"(UTC+02:00) Cairo (EET / no DST)",       120,  0, 0},
  {"(UTC+02:00) Johannesburg (SAST)",        120,  0, 0},
  {"(UTC+02:00) Harare (CAT)",               120,  0, 0},
  {"(UTC+03:00) Moscow (MSK)",               180,  0, 0},
  {"(UTC+03:00) Riyadh / Nairobi",           180,  0, 0},
  {"(UTC+03:30) Tehran (IRST/no DST)",       210,  0, 0},
  {"(UTC+04:00) Dubai / Baku (GST)",         240,  0, 0},
  {"(UTC+04:30) Kabul (AFT)",                270,  0, 0},
  {"(UTC+05:00) Karachi (PKT)",              300,  0, 0},
  {"(UTC+05:30) Mumbai / Delhi (IST)",       330,  0, 0},
  {"(UTC+05:45) Kathmandu (NPT)",            345,  0, 0},
  {"(UTC+06:00) Dhaka (BST)",                360,  0, 0},
  {"(UTC+06:30) Yangon (MMT)",               390,  0, 0},
  {"(UTC+07:00) Bangkok (ICT)",              420,  0, 0},
  {"(UTC+08:00) Beijing / Singapore (CST)",  480,  0, 0},
  {"(UTC+08:00) Perth (AWST / no DST)",      480,  0, 0},
  {"(UTC+09:00) Tokyo (JST)",                540,  0, 0},
  {"(UTC+09:00) Seoul (KST)",                540,  0, 0},
  {"(UTC+09:30) Adelaide (ACST)",            570, 60, 3},
  {"(UTC+10:00) Sydney / Melbourne (AEST)",  600, 60, 3},
  {"(UTC+10:00) Brisbane (AEST / no DST)",   600,  0, 0},
  {"(UTC+11:00) Noumea (NCT)",               660,  0, 0},
  {"(UTC+12:00) Auckland (NZST)",            720, 60, 3},
  {"(UTC+12:00) Fiji (FJT / no DST)",        720,  0, 0},
  {"(UTC+13:00) Apia (WST)",                 780,  0, 0},
};
const uint8_t TIMEZONE_COUNT = sizeof(timezones) / sizeof(timezones[0]);

// ===== DYNAMIC COLOR VARIABLES =====
uint16_t clockColor;
uint16_t bgColor;
uint16_t sandColor;

// ===== DISPLAY OBJECTS =====
Adafruit_GC9A01A tft1(TFT1_CS, TFT1_DC, TFT1_RST);
Adafruit_GC9A01A tft2(TFT2_CS, TFT2_DC, TFT2_RST);

// ===== RTC =====
RTC_DS3231 rtc;

// ===== WiFi / NTP / Web server =====
WiFiUDP       ntpUDP;
NTPClient     timeClient(ntpUDP, "time.google.com", 0, 60000);
WebServer     server(80);
bool          ntpSynced    = false;
unsigned long lastSyncTime = 0;
const unsigned long syncInterval = 86400000UL;

// ===== CONFIGURATION =====
bool    configHourMode        = false;
char    configNtpServer[64]   = "time.google.com";
uint8_t configTimezoneIndex   = 8;     // Default: New York (EST)
bool    configDstEnabled      = true;  // Default: DST on
uint8_t configClockColorIndex = 6;     // Default: Cyan
uint8_t configBgColorIndex    = 0;     // Default: Black
uint8_t configSandColorIndex  = 12;    // Default: Sand
bool    configIs24Hour        = false; // Default: 12-hour

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

// ============================================================
//  HELPER: Unique device ID from MAC address
// ============================================================
String getDeviceId() {
  uint64_t chipid = ESP.getEfuseMac();
  char id[12];
  snprintf(id, sizeof(id), "%04X%08X",
    (uint32_t)(chipid >> 32), (uint32_t)(chipid & 0xFFFFFFFF));
  return String(id);
}

// ============================================================
//  DST HELPERS
// ============================================================

// Day of week (0=Sunday ... 6=Saturday) — Tomohiko Sakamoto algorithm
int dayOfWeek(int year, int month, int day) {
  static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
  if (month < 3) year--;
  return (year + year/4 - year/100 + year/400 + t[month-1] + day) % 7;
}

// Day number of the nth weekday in a given month
// weekday: 0=Sunday ... 6=Saturday
// nth: 1,2,3,4  or  -1 for last occurrence
int nthWeekdayOfMonth(int year, int month, int weekday, int nth) {
  int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (month == 2 && ((year%4==0 && year%100!=0) || year%400==0))
    daysInMonth[1] = 29;
  int firstDow = dayOfWeek(year, month, 1);
  int first    = 1 + ((weekday - firstDow + 7) % 7);
  if (nth == -1) {
    int last = first;
    while (last + 7 <= daysInMonth[month-1]) last += 7;
    return last;
  }
  return first + (nth - 1) * 7;
}

// Returns true if local STANDARD time falls within a DST period
bool isInDST_Rule(int year, int month, int day, int hour, int dstRule) {
  if (dstRule == 0) return false;
  const int H = 2; // transition hour (2:00 AM local standard time)

  if (dstRule == 1) { // US/Canada: 2nd Sun Mar — 1st Sun Nov
    int s = nthWeekdayOfMonth(year,  3, 0,  2);
    int e = nthWeekdayOfMonth(year, 11, 0,  1);
    if (month > 3 && month < 11) return true;
    if (month ==  3) return (day > s) || (day == s && hour >= H);
    if (month == 11) return (day < e) || (day == e && hour <  H);
    return false;
  }
  if (dstRule == 2) { // EU/UK: last Sun Mar — last Sun Oct
    int s = nthWeekdayOfMonth(year,  3, 0, -1);
    int e = nthWeekdayOfMonth(year, 10, 0, -1);
    if (month > 3 && month < 10) return true;
    if (month ==  3) return (day > s) || (day == s && hour >= H);
    if (month == 10) return (day < e) || (day == e && hour <  H);
    return false;
  }
  if (dstRule == 3) { // Southern Hemisphere: 1st Sun Oct — 1st Sun Apr
    int s = nthWeekdayOfMonth(year, 10, 0, 1);
    int e = nthWeekdayOfMonth(year,  4, 0, 1);
    if (month > 10 || month < 4) return true;
    if (month >= 5 && month <= 9) return false;
    if (month == 10) return (day > s) || (day == s && hour >= H);
    if (month ==  4) return (day < e) || (day == e && hour <  H);
    return false;
  }
  return false;
}

// Returns the effective UTC offset in minutes, DST-adjusted
int32_t getEffectiveOffset(time_t utcTime) {
  if (configTimezoneIndex >= TIMEZONE_COUNT) return -300;
  const TimezoneEntry& tz = timezones[configTimezoneIndex];
  int32_t offset = tz.standardOffset;
  if (configDstEnabled && tz.dstOffset > 0 && tz.dstRule != 0) {
    // Use local standard time to evaluate the DST boundary
    time_t stdLocal = utcTime + (int32_t)(offset * 60);
    DateTime sdt(stdLocal);
    if (isInDST_Rule(sdt.year(), sdt.month(), sdt.day(), sdt.hour(), tz.dstRule))
      offset += tz.dstOffset;
  }
  return offset;
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
  applyColors();

  tft1.begin(); tft2.begin();
  tft1.setRotation(2); tft2.setRotation(0);
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
  int32_t effOffset = getEffectiveOffset(utc);
  time_t local = utc + (effOffset * 60);
  DateTime localTime(local);

  currentMinute     = localTime.minute();
  hour12            = localTime.hour();
  currentHour       = (hour12 == 0) ? 12 : ((hour12 > 12) ? hour12 - 12 : hour12);
  lastMinuteTracked = currentMinute;

  int secondsIn = HOUR_MODE
    ? (localTime.minute() * 60 + localTime.second())
    : localTime.second();
  animationStartMillis = millis() - (secondsIn * 1000UL);

  bool dstNow = configDstEnabled && (effOffset != timezones[configTimezoneIndex].standardOffset);
  Serial.printf("TZ: %s  Offset: %d min  DST: %s  Time: %02d:%02d:%02d  Mode: %s  Fmt: %s\n",
    timezones[configTimezoneIndex].name, effOffset,
    dstNow ? "ACTIVE" : "inactive",
    localTime.hour(), localTime.minute(), localTime.second(),
    HOUR_MODE ? "HOUR" : "MIN",
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

  char modeStr[2]; snprintf(modeStr, sizeof(modeStr), "%d", configHourMode ? 1 : 0);
  char tzStr[4];   snprintf(tzStr,   sizeof(tzStr),   "%d", configTimezoneIndex);
  char dstStr[2];  snprintf(dstStr,  sizeof(dstStr),  "%d", configDstEnabled ? 1 : 0);
  char clkStr[3];  snprintf(clkStr,  sizeof(clkStr),  "%d", configClockColorIndex);
  char bgStr[3];   snprintf(bgStr,   sizeof(bgStr),   "%d", configBgColorIndex);
  char sandStr[3]; snprintf(sandStr, sizeof(sandStr), "%d", configSandColorIndex);
  char fmtStr[2];  snprintf(fmtStr,  sizeof(fmtStr),  "%d", configIs24Hour ? 1 : 0);

  WiFiManagerParameter paramNtp  ("ntp",  "NTP Server",              configNtpServer, 64);
  WiFiManagerParameter paramMode ("mode", "Timer (0=Min 1=Hr)",      modeStr,          2);
  WiFiManagerParameter paramTz   ("tz",   "Timezone Index (0-44)",   tzStr,            4);
  WiFiManagerParameter paramDst  ("dst",  "DST (0=Off 1=On)",        dstStr,           2);
  WiFiManagerParameter paramClk  ("clk",  "Clock Color (0-13)",      clkStr,           3);
  WiFiManagerParameter paramBg   ("bg",   "BG Color (0-13)",         bgStr,            3);
  WiFiManagerParameter paramSand ("sand", "Sand Color (0-13)",       sandStr,          3);
  WiFiManagerParameter paramFmt  ("fmt",  "Format (0=12hr 1=24hr)",  fmtStr,           2);

  wifiManager.addParameter(&paramNtp);
  wifiManager.addParameter(&paramMode);
  wifiManager.addParameter(&paramTz);
  wifiManager.addParameter(&paramDst);
  wifiManager.addParameter(&paramClk);
  wifiManager.addParameter(&paramBg);
  wifiManager.addParameter(&paramSand);
  wifiManager.addParameter(&paramFmt);

  tft1.fillRect(0, 55, 240, 90, bgColor);
  tft1.setTextColor(clockColor);
  tft1.setTextSize(2);
  tft1.setCursor(10, 65);
  tft1.print("WiFi...");
  tft1.setTextSize(1);
  tft1.setCursor(10, 90);
  tft1.print("SSID: Hourglass-");
  tft1.setCursor(10, 102);
  tft1.print(getDeviceId());

  String apName = "Hourglass-" + getDeviceId();
  if (!wifiManager.autoConnect(apName.c_str())) {
    Serial.println("WiFi connection failed.");
    tft1.fillRect(0, 55, 240, 90, bgColor);
    return;
  }

  Serial.print("WiFi connected. IP: ");
  Serial.println(WiFi.localIP());

  if (portalConfigSaved) {
    const char* v;
    v = paramNtp.getValue();
    if (strlen(v) > 0) { strncpy(configNtpServer, v, 63); configNtpServer[63] = '\0'; }

    v = paramMode.getValue();
    if (strlen(v) > 0) { configHourMode = (v[0] == '1'); HOUR_MODE = configHourMode; }

    v = paramTz.getValue();
    if (strlen(v) > 0) { uint8_t i = (uint8_t)atoi(v); if (i < TIMEZONE_COUNT) configTimezoneIndex = i; }

    v = paramDst.getValue();
    if (strlen(v) > 0) configDstEnabled = (v[0] == '1');

    v = paramClk.getValue();
    if (strlen(v) > 0) { uint8_t i = (uint8_t)atoi(v); if (i < COLOR_PALETTE_COUNT) configClockColorIndex = i; }

    v = paramBg.getValue();
    if (strlen(v) > 0) { uint8_t i = (uint8_t)atoi(v); if (i < COLOR_PALETTE_COUNT) configBgColorIndex = i; }

    v = paramSand.getValue();
    if (strlen(v) > 0) { uint8_t i = (uint8_t)atoi(v); if (i < COLOR_PALETTE_COUNT) configSandColorIndex = i; }

    v = paramFmt.getValue();
    if (strlen(v) > 0) configIs24Hour = (v[0] == '1');

    saveConfig();
    applyColors();
  }

  startWebServer();
  tft1.fillRect(0, 55, 240, 90, bgColor);
}

// ============================================================
//  WEB SERVER
// ============================================================
void startWebServer() {

  // ---- Status page ----
  server.on("/", HTTP_GET, []() {
    time_t utc = rtc.now().unixtime();
    int32_t effOff = getEffectiveOffset(utc);
    bool dstActive = configDstEnabled &&
                     (effOff != timezones[configTimezoneIndex].standardOffset);

    String html = "<!DOCTYPE html><html><head><title>Hourglass</title>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>body{font-family:sans-serif;padding:20px;background:#f0f0f0;}";
    html += "h1{text-align:center;}";
    html += ".card{background:#fff;padding:15px;border-radius:8px;margin:10px 0;}";
    html += "a.btn{display:block;text-align:center;padding:14px;background:#2196F3;";
    html += "color:#fff;text-decoration:none;border-radius:6px;margin-top:10px;font-size:16px;}";
    html += "</style></head><body>";
    html += "<h1>&#8987; Hourglass</h1><div class='card'>";
    html += "<p><b>Mode:</b> "       + String(HOUR_MODE ? "Hour (1 hr/cycle)" : "Minute (1 min/cycle)") + "</p>";
    html += "<p><b>Format:</b> "     + String(configIs24Hour ? "24-Hour" : "12-Hour") + "</p>";
    html += "<p><b>Timezone:</b> "   + String(timezones[configTimezoneIndex].name) + "</p>";
    html += "<p><b>DST:</b> "        + String(configDstEnabled ? "Enabled" : "Disabled");
    if (configDstEnabled)
      html += " &mdash; currently <b>" + String(dstActive ? "active" : "inactive") + "</b>";
    html += "</p>";
    html += "<p><b>NTP:</b> "        + String(configNtpServer) + "</p>";
    html += "<p><b>Clock Color:</b> "+ String(colorNames[configClockColorIndex]) + "</p>";
    html += "<p><b>Background:</b> " + String(colorNames[configBgColorIndex]) + "</p>";
    html += "<p><b>Sand Color:</b> " + String(colorNames[configSandColorIndex]) + "</p>";
    html += "<p><b>IP:</b> "         + WiFi.localIP().toString() + "</p>";
    html += "</div>";
    html += "<a class='btn' href='/config'>&#9881; Configure</a>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  });

  // ---- Config page ----
  server.on("/config", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head><title>Configure</title>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>body{font-family:sans-serif;padding:20px;background:#f0f0f0;}";
    html += "h1,h2{text-align:center;}";
    html += "label{display:block;margin:14px 0 4px;font-weight:bold;}";
    html += "input,select{width:100%;padding:10px;box-sizing:border-box;";
    html += "border:1px solid #ccc;border-radius:4px;font-size:15px;}";
    html += ".btn{display:block;width:100%;padding:14px;border:none;border-radius:6px;";
    html += "font-size:16px;margin-top:10px;cursor:pointer;text-align:center;";
    html += "text-decoration:none;box-sizing:border-box;color:#fff;}";
    html += ".btn-green{background:#4CAF50;}.btn-green:hover{background:#388E3C;}";
    html += ".btn-orange{background:#FF9800;}.btn-orange:hover{background:#F57C00;}";
    html += ".btn-red{background:#e53935;}.btn-red:hover{background:#b71c1c;}";
    html += ".danger{background:#fff;padding:15px;border-radius:8px;";
    html += "margin-top:24px;border:2px solid #e53935;}";
    html += "</style></head><body>";
    html += "<h1>&#9881; Configure</h1>";
    html += "<form action='/save' method='POST'>";

    // Timer Mode
    html += "<label>Timer Mode</label><select name='mode'>";
    html += HOUR_MODE
      ? "<option value='hour' selected>Hour Mode (1 hr/cycle)</option><option value='minute'>Minute Mode (1 min/cycle)</option>"
      : "<option value='minute' selected>Minute Mode (1 min/cycle)</option><option value='hour'>Hour Mode (1 hr/cycle)</option>";
    html += "</select>";

    // Time Format
    html += "<label>Time Format</label><select name='format'>";
    html += configIs24Hour
      ? "<option value='24' selected>24-Hour</option><option value='12'>12-Hour</option>"
      : "<option value='12' selected>12-Hour</option><option value='24'>24-Hour</option>";
    html += "</select>";

    // Timezone
    html += "<label>Timezone</label><select name='tz'>";
    for (uint8_t i = 0; i < TIMEZONE_COUNT; i++) {
      html += "<option value='" + String(i) + "'";
      if (configTimezoneIndex == i) html += " selected";
      html += ">" + String(timezones[i].name) + "</option>";
    }
    html += "</select>";

    // DST
    html += "<label>Daylight Saving Time</label><select name='dst'>";
    html += configDstEnabled
      ? "<option value='1' selected>Enabled (Automatic)</option><option value='0'>Disabled</option>"
      : "<option value='0' selected>Disabled</option><option value='1'>Enabled (Automatic)</option>";
    html += "</select>";

    // NTP Server
    html += "<label>NTP Server</label>";
    html += "<input type='text' name='ntp' value='" + String(configNtpServer) + "' maxlength='63'>";

    // Clock Color
    html += "<label>Clock Color</label><select name='clock'>";
    for (int i = 0; i < COLOR_PALETTE_COUNT; i++) {
      html += "<option value='" + String(i) + "'" + (configClockColorIndex == i ? " selected" : "") + ">";
      html += String(colorNames[i]) + "</option>";
    }
    html += "</select>";

    // Background Color
    html += "<label>Background Color</label><select name='bg'>";
    for (int i = 0; i < COLOR_PALETTE_COUNT; i++) {
      html += "<option value='" + String(i) + "'" + (configBgColorIndex == i ? " selected" : "") + ">";
      html += String(colorNames[i]) + "</option>";
    }
    html += "</select>";

    // Sand Color
    html += "<label>Sand Color</label><select name='sand'>";
    for (int i = 0; i < COLOR_PALETTE_COUNT; i++) {
      html += "<option value='" + String(i) + "'" + (configSandColorIndex == i ? " selected" : "") + ">";
      html += String(colorNames[i]) + "</option>";
    }
    html += "</select>";

    html += "<button type='submit' class='btn btn-green'>&#128190; Save &amp; Restart</button>";
    html += "</form>";

    // ---- Danger Zone ----
    html += "<div class='danger'><h2>&#9888; Danger Zone</h2>";
    html += "<a class='btn btn-orange' href='/wifi_reset'>&#8644; Reset WiFi Credentials</a>";
    html += "<a class='btn btn-red' href='/factory_reset'>&#128465; Factory Reset</a>";
    html += "</div>";

    html += "<p style='text-align:center;color:#999;margin-top:14px;'>IP: ";
    html += WiFi.localIP().toString() + "</p></body></html>";
    server.send(200, "text/html", html);
  });

  // ---- Save ----
  server.on("/save", HTTP_POST, []() {
    configHourMode = (server.arg("mode") == "hour"); HOUR_MODE = configHourMode;
    configIs24Hour = (server.arg("format") == "24");
    configDstEnabled = (server.arg("dst") == "1");

    uint8_t tzIdx = (uint8_t)server.arg("tz").toInt();
    if (tzIdx < TIMEZONE_COUNT) configTimezoneIndex = tzIdx;

    String ntpArg = server.arg("ntp");
    if (ntpArg.length() > 0 && ntpArg.length() < 64) {
      strncpy(configNtpServer, ntpArg.c_str(), 63);
      configNtpServer[63] = '\0';
    }
    uint8_t ci = (uint8_t)server.arg("clock").toInt(); if (ci < COLOR_PALETTE_COUNT) configClockColorIndex = ci;
    uint8_t bi = (uint8_t)server.arg("bg").toInt();    if (bi < COLOR_PALETTE_COUNT) configBgColorIndex = bi;
    uint8_t si = (uint8_t)server.arg("sand").toInt();  if (si < COLOR_PALETTE_COUNT) configSandColorIndex = si;

    saveConfig();
    applyColors();

    String html = "<!DOCTYPE html><html><head><title>Saved</title>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<meta http-equiv='refresh' content='5;url=/'>";
    html += "<style>body{font-family:sans-serif;padding:30px;text-align:center;background:#f0f0f0;}";
    html += "h1{color:#4CAF50;}</style></head><body>";
    html += "<h1>&#10003; Saved!</h1><p>Restarting in 3 seconds&hellip;</p></body></html>";
    server.send(200, "text/html", html);
    delay(3000);
    ESP.restart();
  });

  // ---- WiFi Reset: Confirmation ----
  server.on("/wifi_reset", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head><title>Reset WiFi</title>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>body{font-family:sans-serif;padding:30px;text-align:center;background:#f0f0f0;}";
    html += "h1{color:#FF9800;}";
    html += ".card{background:#fff;padding:20px;border-radius:8px;max-width:420px;margin:0 auto;}";
    html += ".btn{display:inline-block;padding:14px 24px;border:none;border-radius:6px;";
    html += "font-size:16px;margin:8px;cursor:pointer;text-decoration:none;color:#fff;}";
    html += ".btn-orange{background:#FF9800;}.btn-grey{background:#9E9E9E;}";
    html += "</style></head><body><div class='card'>";
    html += "<h1>&#8644; Reset WiFi?</h1>";
    html += "<p>This will erase your saved WiFi credentials. The device will restart ";
    html += "and create its setup hotspot so you can connect to a new network.</p>";
    html += "<p><b>Your timezone, colors, and all other settings will NOT be affected.</b></p>";
    html += "<form action='/do_wifi_reset' method='POST'>";
    html += "<input type='hidden' name='confirm' value='yes'>";
    html += "<button type='submit' class='btn btn-orange'>Yes, Reset WiFi</button>";
    html += "</form>";
    html += "<a class='btn btn-grey' href='/config'>Cancel</a>";
    html += "</div></body></html>";
    server.send(200, "text/html", html);
  });

  // ---- WiFi Reset: Perform ----
  server.on("/do_wifi_reset", HTTP_POST, []() {
    if (server.arg("confirm") != "yes") {
      server.sendHeader("Location", "/config");
      server.send(302);
      return;
    }
    String html = "<!DOCTYPE html><html><head><title>Resetting WiFi</title>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>body{font-family:sans-serif;padding:30px;text-align:center;background:#f0f0f0;}";
    html += "h1{color:#FF9800;}</style></head><body>";
    html += "<h1>&#8644; WiFi Credentials Cleared</h1>";
    html += "<p>The device will restart in hotspot mode. Connect to your Hourglass SSID to reconfigure WiFi.</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
    delay(2000);
    WiFiManager wm;
    wm.resetSettings();
    delay(500);
    ESP.restart();
  });

  // ---- Factory Reset: Confirmation ----
  server.on("/factory_reset", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head><title>Factory Reset</title>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>body{font-family:sans-serif;padding:30px;text-align:center;background:#f0f0f0;}";
    html += "h1{color:#e53935;}";
    html += ".card{background:#fff;padding:20px;border-radius:8px;max-width:420px;";
    html += "margin:0 auto;border:2px solid #e53935;}";
    html += ".btn{display:inline-block;padding:14px 24px;border:none;border-radius:6px;";
    html += "font-size:16px;margin:8px;cursor:pointer;text-decoration:none;color:#fff;}";
    html += ".btn-red{background:#e53935;}.btn-grey{background:#9E9E9E;}";
    html += "</style></head><body><div class='card'>";
    html += "<h1>&#128465; Factory Reset?</h1>";
    html += "<p>This will erase <b>ALL</b> saved settings including WiFi credentials, ";
    html += "timezone, colors, and all other configuration.</p>";
    html += "<p>The device will restart with factory defaults. You will need to ";
    html += "reconfigure everything from scratch.</p>";
    html += "<p><b>This action cannot be undone.</b></p>";
    html += "<form action='/do_factory_reset' method='POST'>";
    html += "<input type='hidden' name='confirm' value='yes'>";
    html += "<button type='submit' class='btn btn-red'>&#9888; Yes, Factory Reset</button>";
    html += "</form>";
    html += "<a class='btn btn-grey' href='/config'>Cancel</a>";
    html += "</div></body></html>";
    server.send(200, "text/html", html);
  });

  // ---- Factory Reset: Perform ----
  server.on("/do_factory_reset", HTTP_POST, []() {
    if (server.arg("confirm") != "yes") {
      server.sendHeader("Location", "/config");
      server.send(302);
      return;
    }
    String html = "<!DOCTYPE html><html><head><title>Factory Reset</title>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>body{font-family:sans-serif;padding:30px;text-align:center;background:#f0f0f0;}";
    html += "h1{color:#e53935;}</style></head><body>";
    html += "<h1>&#128465; Factory Reset Complete</h1>";
    html += "<p>All settings erased. The device will restart and enter first-time setup mode.</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
    delay(2000);
    for (int i = 0; i < 512; i++) EEPROM.write(i, 0xFF);
    EEPROM.commit();
    WiFiManager wm;
    wm.resetSettings();
    delay(500);
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
  EEPROM.write(165, configTimezoneIndex);
  EEPROM.write(166, configDstEnabled ? 1 : 0);
  EEPROM.write(169, configClockColorIndex);
  EEPROM.write(170, configBgColorIndex);
  EEPROM.write(171, configSandColorIndex);
  EEPROM.write(172, configIs24Hour ? 1 : 0);
  EEPROM.write(CONFIG_VALID_ADDR, CONFIG_VALID_BYTE);
  EEPROM.commit();
  Serial.printf("Saved: mode=%d  tz=%d(%s)  dst=%d  fmt=%s  clk=%d  bg=%d  sand=%d\n",
    HOUR_MODE, configTimezoneIndex, timezones[configTimezoneIndex].name,
    configDstEnabled, configIs24Hour ? "24h" : "12h",
    configClockColorIndex, configBgColorIndex, configSandColorIndex);
}

void loadConfig() {
  if (EEPROM.read(CONFIG_VALID_ADDR) != CONFIG_VALID_BYTE) {
    Serial.println("No valid config found — using defaults.");
    return;
  }
  configHourMode = (EEPROM.read(100) == 1); HOUR_MODE = configHourMode;

  for (int i = 0; i < 64; i++) configNtpServer[i] = (char)EEPROM.read(101 + i);
  configNtpServer[63] = '\0';
  if (!configNtpServer[0] || (uint8_t)configNtpServer[0] == 0xFF)
    strncpy(configNtpServer, "time.google.com", 63);

  uint8_t tzIdx = EEPROM.read(165);
  configTimezoneIndex = (tzIdx < TIMEZONE_COUNT) ? tzIdx : 8;
  configDstEnabled    = (EEPROM.read(166) == 1);

  uint8_t ci = EEPROM.read(169); configClockColorIndex = (ci < COLOR_PALETTE_COUNT) ? ci : 6;
  uint8_t bi = EEPROM.read(170); configBgColorIndex    = (bi < COLOR_PALETTE_COUNT) ? bi : 0;
  uint8_t si = EEPROM.read(171); configSandColorIndex  = (si < COLOR_PALETTE_COUNT) ? si : 12;
  configIs24Hour = (EEPROM.read(172) == 1);

  Serial.printf("Loaded: mode=%s  tz=%d(%s)  dst=%d  fmt=%s  clk=%d  bg=%d  sand=%d\n",
    HOUR_MODE ? "hour" : "minute", configTimezoneIndex,
    timezones[configTimezoneIndex].name, configDstEnabled,
    configIs24Hour ? "24h" : "12h",
    configClockColorIndex, configBgColorIndex, configSandColorIndex);
}

// ============================================================
//  APPLY COLORS
// ============================================================
void applyColors() {
  clockColor = colorPalette[configClockColorIndex];
  bgColor    = colorPalette[configBgColorIndex];
  sandColor  = colorPalette[configSandColorIndex];
}

// ============================================================
//  NTP SYNC
// ============================================================
void syncRTCwithNTP() {
  if (WiFi.status() != WL_CONNECTED) { Serial.println("No WiFi — NTP skipped."); return; }
  Serial.println("Syncing NTP...");
  timeClient.begin();
  timeClient.setPoolServerName(configNtpServer);
  int attempts = 0;
  while (!timeClient.update() && attempts < 10) { delay(500); attempts++; yield(); }
  if (timeClient.isTimeSet()) {
    unsigned long epoch = timeClient.getEpochTime();
    rtc.adjust(DateTime(epoch));
    ntpSynced = true; lastSyncTime = millis();
    EEPROM.put(NTP_SYNC_TIME_ADDR, lastSyncTime); EEPROM.commit();
    int32_t effOff = getEffectiveOffset((time_t)epoch);
    time_t local   = (time_t)epoch + (effOff * 60);
    DateTime ld(local);
    bool dstActive = configDstEnabled && (effOff != timezones[configTimezoneIndex].standardOffset);
    Serial.printf("NTP OK — Local: %02d:%02d:%02d  TZ: %s  Offset: %d min  DST: %s\n",
      ld.hour(), ld.minute(), ld.second(),
      timezones[configTimezoneIndex].name, effOff,
      dstActive ? "active" : "inactive");
  } else {
    Serial.println("NTP sync failed.");
    ntpSynced = false;
  }
}

// ============================================================
//  MAIN LOOP
// ============================================================
void loop() {
  server.handleClient();

  time_t utc = rtc.now().unixtime();
  int32_t effOffset = getEffectiveOffset(utc);
  time_t local = utc + (effOffset * 60);
  DateTime now(local);

  currentMinute = now.minute();
  hour12        = now.hour();
  currentHour   = (hour12 == 0) ? 12 : ((hour12 > 12) ? hour12 - 12 : hour12);

  if (currentMinute != lastMinuteTracked) {
    lastMinuteTracked = currentMinute;
    bool dstActive = configDstEnabled && (effOffset != timezones[configTimezoneIndex].standardOffset);
    Serial.printf("Tick: %02d:%02d  mode=%s  DST=%s\n",
      currentHour, currentMinute,
      HOUR_MODE ? "HOUR" : "MIN",
      dstActive ? "active" : "inactive");
    if (!HOUR_MODE) { animationStartMillis = millis(); needsFullRedraw = true; }
    else            { needsTimeUpdate = true; }
  }

  unsigned long elapsed          = millis() - animationStartMillis;
  unsigned long intervalDuration = HOUR_MODE ? 3600000UL : 60000UL;

  if (HOUR_MODE && elapsed >= intervalDuration) {
    animationStartMillis = millis(); elapsed = 0;
    needsFullRedraw = true;
    Serial.println("Hour complete — resetting animation.");
  }

  int currentFrame = (int)constrain(
    (long)(elapsed / (intervalDuration / totalFrames)),
    0L, (long)(totalFrames - 1));

  if (millis() - lastStreamUpdate >= (HOUR_MODE ? 400UL : 150UL)) {
    lastStreamUpdate = millis();
    streamOffset = (streamOffset + 12) % 240;
  }

  drawFrame(currentFrame);

  if (ntpSynced && millis() - lastSyncTime > syncInterval) {
    Serial.println("24h elapsed — re-syncing NTP...");
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
      int w = 0;
      while (WiFi.status() != WL_CONNECTED && w < 20) { delay(500); w++; yield(); }
    }
    syncRTCwithNTP();
    time_t su = rtc.now().unixtime();
    int32_t so = getEffectiveOffset(su);
    time_t sl = su + (so * 60);
    DateTime slocal(sl);
    int secs = HOUR_MODE ? (slocal.minute() * 60 + slocal.second()) : slocal.second();
    animationStartMillis = millis() - (secs * 1000UL);
    needsFullRedraw = true;
  }

  yield();
  delay(50);
}

// ============================================================
//  DRAW TIME DIGITS ONLY
// ============================================================
void drawTimeDisplay() {
  tft1.fillRect(55, 0, 185, 55, bgColor);
  tft1.setCursor(60, 15);
  tft1.setTextColor(clockColor);
  tft1.setTextSize(4);
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
    tft1.fillScreen(bgColor); tft2.fillScreen(bgColor);
    drawTimeDisplay();
    if (frame > 0) {
      m = frame;
      int upperY   = (int)(m * 0.9f + 68);
      int upperTip = (int)(m * 1.2f + 90);
      tft1.fillRect(0, upperY, 240, 240 - upperY, sandColor);
      tft1.fillTriangle(0, upperY, 240, upperY, 120, upperTip, bgColor);
      n = map(m, 0, totalFrames, 250, 75);
      int lowerY = (int)(n * 0.55f + 105);
      if (lowerY < 240) {
        tft2.fillTriangle(0, lowerY-1, 240, lowerY-1, 120, n, sandColor);
        tft2.fillRect(0, lowerY-1, 240, 241-lowerY, sandColor);
      }
      drawSandStream(n);
    } else {
      tft1.fillRect(0, 100, 240, 140, sandColor);
    }
    needsFullRedraw = false; lastFrame = frame; yield(); return;
  }

  if (needsTimeUpdate) drawTimeDisplay();
  if (frame == lastFrame) {
    if (m > 0 && m < totalFrames - 1) drawSandStream(n);
    return;
  }

  m = frame;
  int upperY   = (int)(m * 0.9f + 68);
  int upperTip = (int)(m * 1.2f + 90);
  tft1.fillTriangle(0, upperY, 240, upperY, 120, upperTip, bgColor);
  n = map(m, 0, totalFrames, 250, 75);
  int lowerY = (int)(n * 0.55f + 105);
  if (lowerY < 240) {
    tft2.fillTriangle(0, lowerY-1, 240, lowerY-1, 120, n, sandColor);
    tft2.fillRect(0, lowerY-1, 240, 241-lowerY, sandColor);
  }
  if (m < totalFrames - 1) drawSandStream(n);
  lastFrame = frame; yield();
}

// ============================================================
//  DRAW SAND STREAM
// ============================================================
void drawSandStream(int bottomY) {
  int streamBottom = min(bottomY - 10, 240);
  if (streamBottom <= 0) return;
  tft2.fillRect(STREAM_CLEAR_X, 0, STREAM_CLEAR_W, streamBottom, bgColor);
  for (int i = 0; i < streamBottom; i += 8) {
    int pos = (i + streamOffset) % streamBottom;
    if (pos < 0 || pos >= streamBottom) continue;
    int xOff   = constrain((int)random(-4, 5), -(STREAM_X_HALF-2), (STREAM_X_HALF-2));
    int radius = (int)random(1, 3);
    int cx     = STREAM_CX + xOff;
    if (cx-radius >= STREAM_CLEAR_X && cx+radius < STREAM_CLEAR_X+STREAM_CLEAR_W)
      tft2.fillCircle(cx, pos, radius, sandColor);
    if (random(3) == 0) {
      int cx2 = constrain(cx+(int)random(-2,3), STREAM_CLEAR_X+1, STREAM_CLEAR_X+STREAM_CLEAR_W-2);
      int cy2 = pos + (int)random(-3, 4);
      if (cy2 >= 0 && cy2 < streamBottom)
        tft2.fillCircle(cx2, cy2, 1, sandColor);
    }
  }
  for (int i = 0; i < 5; i++) {
    int gy = bottomY + (int)random(-12, -2);
    int gx = STREAM_CX + (int)random(-5, 6);
    int gr = (int)random(1, 3);
    if (gy > 0 && gy < 240 && gx-gr >= STREAM_CLEAR_X && gx+gr < STREAM_CLEAR_X+STREAM_CLEAR_W)
      tft2.fillCircle(gx, gy, gr, sandColor);
  }
  for (int i = 0; i < 3; i++) {
    int dx = STREAM_CX + (int)random(-5, 6);
    int dy = (int)random(0, streamBottom);
    if (dx >= STREAM_CLEAR_X && dx < STREAM_CLEAR_X+STREAM_CLEAR_W)
      tft2.drawPixel(dx, dy, sandColor);
  }
}
