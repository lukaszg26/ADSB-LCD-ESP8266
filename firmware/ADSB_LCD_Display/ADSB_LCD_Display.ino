/*
  ADS-B LCD 16x2 EXTENDED v14
  Wemos D1 mini / ESP8266 + LCD 16x2 I2C

  ANIMACJA "NOWY NAJBLIZSZY" - BEZ ZMIAN.

  EKRANY - KAZDY 5 SEKUND:
  1. Typ samolotu / operator
  2. Callsign / registration
  3. Species / engines
  4. Odleglosc km / INCOMING lub OUTCOMING
  5. Predkosc km/h (kt) / Flight Level
  6. Heading + kierunek / Bearing + kierunek
  7. ICAO / Squawk
  8. Liczba wszystkich samolotow VRS

  Najblizszy samolot: promien konfigurowany przez WWW.
  Liczba samolotow VRS: totalAc - cala lista VRS.
  Panel WWW: http://adsb-display.local/ lub adres IP D1 mini.

  LCD -> Wemos D1 mini:
  GND -> GND
  VCC -> 5V
  SDA -> D2 / GPIO4
  SCL -> D1 / GPIO5

  Serial Monitor: 115200
*/

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>
#include <sys/time.h>

// =====================================================
// WIFI
// =====================================================

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// =====================================================
// KONFIGURACJA APLIKACJI - ZAPIS DO LITTLEFS
// =====================================================

struct AppConfig {
  String vrsUrl;

  float rxLat;
  float rxLon;

  uint16_t maxDistanceKm;
  uint16_t dataRefreshSec;
  uint16_t screenDurationSec;

  bool nightEnabled;
  uint8_t nightOffHour;
  uint8_t nightOnHour;

  bool lprAlertEnabled;
  uint8_t lprBlinks;

  float trendThresholdKm;

  bool screenEnabled[8];
};

AppConfig config;

const char* CONFIG_FILE = "/config.json";

// Sprawdzanie harmonogramu podswietlenia co 10 sekund.
const unsigned long BACKLIGHT_CHECK_MS = 10000;

// =====================================================
// SERWER WWW
// =====================================================

ESP8266WebServer webServer(80);

bool webStarted = false;
bool mdnsStarted = false;

bool lastVrsOk = false;
int lastVrsHttpCode = 0;
unsigned long lastVrsSuccessMs = 0;

// =====================================================
// DOMYSLNE USTAWIENIA
// =====================================================

void setDefaultConfig() {
  config.vrsUrl =
    "http://192.168.1.100:8090/VirtualRadar/AircraftList.json";

  config.rxLat = 52.2297;  // example: Warsaw - change in WWW panel
  config.rxLon = 21.0122;  // example: Warsaw - change in WWW panel

  config.maxDistanceKm = 100;
  config.dataRefreshSec = 3;
  config.screenDurationSec = 5;

  config.nightEnabled = true;
  config.nightOffHour = 21;
  config.nightOnHour = 6;

  config.lprAlertEnabled = true;
  config.lprBlinks = 3;

  config.trendThresholdKm = 0.05;

  for (uint8_t i = 0; i < 8; i++) {
    config.screenEnabled[i] = true;
  }
}

bool saveConfig() {
  JsonDocument doc;

  doc["vrs_url"] = config.vrsUrl;
  doc["rx_lat"] = config.rxLat;
  doc["rx_lon"] = config.rxLon;
  doc["max_distance_km"] = config.maxDistanceKm;
  doc["data_refresh_sec"] = config.dataRefreshSec;
  doc["screen_duration_sec"] = config.screenDurationSec;

  doc["night_enabled"] = config.nightEnabled;
  doc["night_off_hour"] = config.nightOffHour;
  doc["night_on_hour"] = config.nightOnHour;

  doc["lpr_alert_enabled"] = config.lprAlertEnabled;
  doc["lpr_blinks"] = config.lprBlinks;

  doc["trend_threshold_km"] = config.trendThresholdKm;

  JsonArray screens = doc["screens"].to<JsonArray>();

  for (uint8_t i = 0; i < 8; i++) {
    screens.add(config.screenEnabled[i]);
  }

  File file = LittleFS.open(CONFIG_FILE, "w");

  if (!file) {
    Serial.println(F("CONFIG: nie mozna otworzyc pliku do zapisu"));
    return false;
  }

  if (serializeJsonPretty(doc, file) == 0) {
    Serial.println(F("CONFIG: blad zapisu JSON"));
    file.close();
    return false;
  }

  file.close();

  Serial.println(F("CONFIG: zapisano /config.json"));
  return true;
}

bool loadConfig() {
  if (!LittleFS.exists(CONFIG_FILE)) {
    Serial.println(F("CONFIG: brak pliku - uzywam ustawien domyslnych"));
    saveConfig();
    return true;
  }

  File file = LittleFS.open(CONFIG_FILE, "r");

  if (!file) {
    Serial.println(F("CONFIG: nie mozna otworzyc pliku"));
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.print(F("CONFIG JSON ERROR: "));
    Serial.println(error.c_str());
    return false;
  }

  config.vrsUrl = doc["vrs_url"] | config.vrsUrl;
  config.rxLat = doc["rx_lat"] | config.rxLat;
  config.rxLon = doc["rx_lon"] | config.rxLon;
  config.maxDistanceKm = doc["max_distance_km"] | config.maxDistanceKm;
  config.dataRefreshSec = doc["data_refresh_sec"] | config.dataRefreshSec;
  config.screenDurationSec = doc["screen_duration_sec"] | config.screenDurationSec;

  config.nightEnabled = doc["night_enabled"] | config.nightEnabled;
  config.nightOffHour = doc["night_off_hour"] | config.nightOffHour;
  config.nightOnHour = doc["night_on_hour"] | config.nightOnHour;

  config.lprAlertEnabled = doc["lpr_alert_enabled"] | config.lprAlertEnabled;
  config.lprBlinks = doc["lpr_blinks"] | config.lprBlinks;

  config.trendThresholdKm = doc["trend_threshold_km"] | config.trendThresholdKm;

  JsonArray screens = doc["screens"].as<JsonArray>();

  if (!screens.isNull()) {
    for (uint8_t i = 0; i < 8 && i < screens.size(); i++) {
      config.screenEnabled[i] = screens[i].as<bool>();
    }
  }

  Serial.println(F("CONFIG: wczytano /config.json"));
  return true;
}

void sanitizeConfig() {
  config.vrsUrl.trim();

  if (!config.vrsUrl.startsWith("http://")) {
    setDefaultConfig();
    saveConfig();
    return;
  }

  if (config.maxDistanceKm < 1) config.maxDistanceKm = 1;
  if (config.maxDistanceKm > 500) config.maxDistanceKm = 500;

  if (config.dataRefreshSec < 1) config.dataRefreshSec = 1;
  if (config.dataRefreshSec > 60) config.dataRefreshSec = 60;

  if (config.screenDurationSec < 1) config.screenDurationSec = 1;
  if (config.screenDurationSec > 30) config.screenDurationSec = 30;

  if (config.nightOffHour > 23) config.nightOffHour = 21;
  if (config.nightOnHour > 23) config.nightOnHour = 6;

  if (config.lprBlinks < 1) config.lprBlinks = 1;
  if (config.lprBlinks > 10) config.lprBlinks = 10;

  if (config.trendThresholdKm < 0.01) config.trendThresholdKm = 0.01;
  if (config.trendThresholdKm > 5.0) config.trendThresholdKm = 5.0;

  bool anyScreen = false;

  for (uint8_t i = 0; i < 8; i++) {
    if (config.screenEnabled[i]) {
      anyScreen = true;
      break;
    }
  }

  if (!anyScreen) {
    config.screenEnabled[0] = true;
  }
}

// =====================================================
// LCD
// =====================================================

const uint8_t I2C_SDA_GPIO = 4;  // D2
const uint8_t I2C_SCL_GPIO = 5;  // D1
const uint8_t LCD_ADDRESS = 0x27;

LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);

byte planeChar[8] = {
  B00100,
  B00100,
  B10101,
  B01110,
  B00100,
  B01110,
  B10101,
  B00000
};

// =====================================================
// DANE SAMOLOTU
// =====================================================

struct AircraftData {
  bool valid = false;

  String call;
  String reg;
  String icao;
  String type;
  String model;
  String oper;
  String opCode;

  String engines;
  int engineType = 0;
  int species = 0;

  int squawk = -1;

  float distance = 0.0;
  float bearing = 0.0;
  float track = 0.0;

  bool trackIsHeading = false;

  int altitude = 0;
  float speed = 0.0;
  bool speedValid = false;
  int speedType = 0;
};

AircraftData aircraft;

String previousAircraftKey = "";
float previousDistance = 0.0;

int8_t distanceTrend = 0;

int totalAircraftCount = 0;

// =====================================================
// CZASY / EKRANY / ANIMACJE
// =====================================================

unsigned long lastDataRefresh = 0;
unsigned long lastScreenChange = 0;
unsigned long lastAnimationFrame = 0;
unsigned long lastModelScroll = 0;
uint16_t modelScrollOffset = 0;

unsigned long lastBacklightCheck = 0;
bool backlightOn = true;
bool timeConfigured = false;
bool vrsTimeSynced = false;
unsigned long lastVrsTimeSyncMs = 0;

uint8_t currentScreen = 0;

bool newAircraftAnimationActive = false;
unsigned long newAircraftAnimationStart = 0;
uint8_t newAircraftFrame = 0;

bool pendingLprAlert = false;

uint8_t scanFrame = 0;

// =====================================================
// PROTOTYPY
// =====================================================

void renderCurrentScreen();
void renderModelLine(bool resetScroll = false);
void applyBacklightSchedule(bool forceLog = false);
void configureTimeNtp();
void startWebServer();

// =====================================================
// LCD HELPERS
// =====================================================

void printLine(uint8_t row, String text) {
  while (text.length() < 16) text += " ";

  if (text.length() > 16) {
    text = text.substring(0, 16);
  }

  lcd.setCursor(0, row);
  lcd.print(text);
}

void clearLine(uint8_t row) {
  lcd.setCursor(0, row);
  lcd.print("                ");
}

String centered(String text) {
  text.trim();

  if (text.length() > 16) {
    text = text.substring(0, 16);
  }

  String out = "";

  int spaces = 16 - text.length();
  int left = spaces / 2;

  for (int i = 0; i < left; i++) {
    out += " ";
  }

  out += text;

  while (out.length() < 16) {
    out += " ";
  }

  return out;
}

String safeText(String text, const String& fallback = "-") {
  text.trim();

  if (text.length() == 0) {
    return fallback;
  }

  return text;
}

String htmlEscape(String value) {
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  value.replace("'", "&#39;");

  return value;
}

String boolBadge(bool ok, const String& yes, const String& no) {
  String html = "<span class='badge ";
  html += ok ? "ok'>" : "bad'>";
  html += ok ? yes : no;
  html += "</span>";
  return html;
}

// =====================================================
// KIERUNKI
// =====================================================

String cardinal(float degrees) {
  while (degrees < 0.0) degrees += 360.0;
  while (degrees >= 360.0) degrees -= 360.0;

  if (degrees >= 337.5 || degrees < 22.5) return "N";
  if (degrees < 67.5)  return "NE";
  if (degrees < 112.5) return "E";
  if (degrees < 157.5) return "SE";
  if (degrees < 202.5) return "S";
  if (degrees < 247.5) return "SW";
  if (degrees < 292.5) return "W";

  return "NW";
}

String speciesName(int species) {
  switch (species) {
    case 1: return "LANDPLANE";
    case 2: return "SEAPLANE";
    case 3: return "AMPHIBIAN";
    case 4: return "HELICOPTER";
    case 5: return "GYROCOPTER";
    case 6: return "TILTWING";
    case 7: return "GROUND VEHICLE";
    case 8: return "TOWER";
    default: return "UNKNOWN";
  }
}

String engineTypeName(int engineType) {
  switch (engineType) {
    case 1: return "PISTON";
    case 2: return "TURBO";
    case 3: return "JET";
    case 4: return "ELECTRIC";
    case 5: return "ROCKET";
    default: return "";
  }
}

String aircraftKey(const String& icao, const String& call, const String& reg) {
  if (icao.length() > 0) return icao;
  if (call.length() > 0) return call;
  return reg;
}

String displayName(const String& call, const String& reg, const String& icao) {
  if (call.length() > 0) return call;
  if (reg.length() > 0) return reg;
  if (icao.length() > 0) return icao;
  return "UNKNOWN";
}

// =====================================================
// CZAS POLSKI
// =====================================================

int daysInMonthUtc(int year, int month) {
  static const int days[] = {
    31, 28, 31, 30, 31, 30,
    31, 31, 30, 31, 30, 31
  };

  if (month == 2) {
    bool leap =
      (year % 4 == 0 && year % 100 != 0) ||
      (year % 400 == 0);

    return leap ? 29 : 28;
  }

  return days[month - 1];
}

int lastSundayOfMonthUtc(int year, int month) {
  struct tm t = {};

  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = daysInMonthUtc(year, month);
  t.tm_hour = 12;

  time_t epoch = mktime(&t);

  struct tm check;
  gmtime_r(&epoch, &check);

  return t.tm_mday - check.tm_wday;
}

bool isPolishDstUtc(time_t utc) {
  struct tm utcTm;
  gmtime_r(&utc, &utcTm);

  int year = utcTm.tm_year + 1900;
  int month = utcTm.tm_mon + 1;
  int day = utcTm.tm_mday;
  int hour = utcTm.tm_hour;

  if (month < 3 || month > 10) {
    return false;
  }

  if (month > 3 && month < 10) {
    return true;
  }

  if (month == 3) {
    int lastSunday = lastSundayOfMonthUtc(year, 3);

    if (day > lastSunday) return true;
    if (day < lastSunday) return false;

    return hour >= 1;
  }

  int lastSunday = lastSundayOfMonthUtc(year, 10);

  if (day < lastSunday) return true;
  if (day > lastSunday) return false;

  return hour < 1;
}

int polishUtcOffsetSeconds(time_t utc) {
  return isPolishDstUtc(utc) ? 7200 : 3600;
}

bool getPolishLocalTime(struct tm &localTm) {
  time_t utc = time(nullptr);

  if (utc < 1700000000) {
    return false;
  }

  time_t polishTime =
    utc + polishUtcOffsetSeconds(utc);

  gmtime_r(&polishTime, &localTm);
  return true;
}

String localTimeString() {
  struct tm tmNow;

  if (!getPolishLocalTime(tmNow)) {
    return "brak synchronizacji";
  }

  char buffer[24];

  snprintf(
    buffer,
    sizeof(buffer),
    "%04d-%02d-%02d %02d:%02d:%02d",
    tmNow.tm_year + 1900,
    tmNow.tm_mon + 1,
    tmNow.tm_mday,
    tmNow.tm_hour,
    tmNow.tm_min,
    tmNow.tm_sec
  );

  return String(buffer);
}

void syncClockFromVrs(uint64_t serverTimeMs) {
  if (serverTimeMs < 1700000000000ULL) {
    Serial.println(F("VRS TIME: nieprawidlowa wartosc stm"));
    return;
  }

  struct timeval tv;

  tv.tv_sec =
    (time_t)(serverTimeMs / 1000ULL);

  tv.tv_usec =
    (suseconds_t)(
      (serverTimeMs % 1000ULL) * 1000ULL
    );

  settimeofday(&tv, nullptr);

  vrsTimeSynced = true;
  lastVrsTimeSyncMs = millis();

  Serial.print(F("CZAS POLSKI: "));
  Serial.println(localTimeString());
}

bool getLocalHour(int &hourOut) {
  struct tm localTm;

  if (!getPolishLocalTime(localTm)) {
    return false;
  }

  hourOut = localTm.tm_hour;
  return true;
}

void configureTimeNtp() {
  setenv("TZ", "UTC0", 1);
  tzset();

  configTime(
    0,
    0,
    "pool.ntp.org",
    "time.google.com",
    "time.nist.gov"
  );

  timeConfigured = true;

  Serial.println(F("NTP: konfiguracja UTC uruchomiona"));
}

// =====================================================
// PODSWIETLENIE
// =====================================================

void setBacklightState(bool on) {
  if (on == backlightOn) {
    return;
  }

  backlightOn = on;

  if (on) {
    lcd.backlight();
  } else {
    lcd.noBacklight();
  }
}

bool isNightDisplayTime() {
  if (!config.nightEnabled) {
    return false;
  }

  int hour;

  if (!getLocalHour(hour)) {
    return false;
  }

  if (config.nightOffHour == config.nightOnHour) {
    return false;
  }

  if (config.nightOffHour > config.nightOnHour) {
    return (
      hour >= config.nightOffHour ||
      hour < config.nightOnHour
    );
  }

  return (
    hour >= config.nightOffHour &&
    hour < config.nightOnHour
  );
}

void applyBacklightSchedule(bool forceLog) {
  bool shouldBeOn = !isNightDisplayTime();

  if (forceLog || shouldBeOn != backlightOn) {
    Serial.print(F("LCD harmonogram: "));
    Serial.print(localTimeString());
    Serial.print(F(" -> "));
    Serial.println(
      shouldBeOn ? F("ON") : F("OFF")
    );
  }

  setBacklightState(shouldBeOn);
}

// =====================================================
// LPR
// =====================================================

bool isLprAircraft(const String& callIn, const String& opIn, const String& opCodeIn) {
  String call = callIn;
  String op = opIn;
  String opCode = opCodeIn;

  call.toUpperCase();
  op.toUpperCase();
  opCode.toUpperCase();

  if (opCode == "LPR") {
    return true;
  }

  if (call.startsWith("LPR")) {
    return true;
  }

  if (op.indexOf("LOTNICZE POGOTOWIE RATUNKOWE") >= 0) {
    return true;
  }

  if (op.indexOf("POLISH MEDICAL AIR RESCUE") >= 0) {
    return true;
  }

  return false;
}

void runLprBlinkAlert() {
  if (!config.lprAlertEnabled) {
    return;
  }

  Serial.println(F("*** LPR ALERT ***"));

  currentScreen = 0;
  lcd.clear();
  renderCurrentScreen();

  setBacklightState(true);
  delay(250);

  for (uint8_t i = 0; i < config.lprBlinks; i++) {
    lcd.noBacklight();
    backlightOn = false;
    delay(250);

    lcd.backlight();
    backlightOn = true;
    delay(450);
  }

  applyBacklightSchedule(true);
  lastScreenChange = millis();
}

// =====================================================
// ANIMACJE
// =====================================================

void startupAnimation() {
  lcd.clear();
  printLine(0, centered("ADS-B RADAR"));

  for (uint8_t x = 0; x < 16; x++) {
    clearLine(1);
    lcd.setCursor(x, 1);
    lcd.write(byte(0));

    delay(65);
    yield();
  }

  printLine(1, centered("START"));
  delay(500);
}

void startNewAircraftAnimation() {
  newAircraftAnimationActive = true;
  newAircraftAnimationStart = millis();
  lastAnimationFrame = 0;
  newAircraftFrame = 0;

  lcd.clear();
  printLine(0, "NOWY NAJBLIZSZY");
}

void updateNewAircraftAnimation() {
  if (!newAircraftAnimationActive) {
    return;
  }

  if (millis() - newAircraftAnimationStart > 2000) {
    newAircraftAnimationActive = false;
    currentScreen = 0;
    lastScreenChange = millis();

    lcd.clear();
    renderCurrentScreen();

    if (pendingLprAlert) {
      pendingLprAlert = false;
      runLprBlinkAlert();
    }

    return;
  }

  if (millis() - lastAnimationFrame < 110) {
    return;
  }

  lastAnimationFrame = millis();

  String name = centered(displayName(aircraft.call, aircraft.reg, aircraft.icao));

  clearLine(1);

  uint8_t planePos = newAircraftFrame % 16;

  lcd.setCursor(0, 1);

  for (uint8_t i = 0; i < 16; i++) {
    if (i == planePos) {
      lcd.write(byte(0));
    }
    else if (newAircraftFrame > 5) {
      lcd.print(name[i]);
    }
    else {
      lcd.print(' ');
    }
  }

  newAircraftFrame++;
}

// =====================================================
// MODEL SCROLL
// =====================================================

void renderModelLine(bool resetScroll) {
  String model = safeText(aircraft.model, aircraft.type);
  model.trim();

  if (resetScroll) {
    modelScrollOffset = 0;
    lastModelScroll = millis();
  }

  if (model.length() <= 16) {
    printLine(0, centered(model));
    return;
  }

  String scrollText = model + "   ";

  if (millis() - lastModelScroll >= 350) {
    lastModelScroll = millis();
    modelScrollOffset++;

    if (modelScrollOffset >= scrollText.length()) {
      modelScrollOffset = 0;
    }
  }

  String window = "";

  for (uint8_t i = 0; i < 16; i++) {
    uint16_t idx =
      (modelScrollOffset + i) % scrollText.length();

    window += scrollText[idx];
  }

  printLine(0, window);
}

// =====================================================
// EKRANY
// =====================================================

void renderScreen1() {
  String op = safeText(
    aircraft.oper,
    "UNKNOWN OPERATOR"
  );

  renderModelLine(true);
  printLine(1, centered(op));
}

void renderScreen2() {
  String call = safeText(
    aircraft.call,
    "NO CALLSIGN"
  );

  String reg = safeText(
    aircraft.reg,
    "NO REG"
  );

  printLine(0, centered("CALL: " + call));
  printLine(1, centered("REG: " + reg));
}

void renderScreen3() {
  String species = speciesName(aircraft.species);
  String engine = "";

  if (aircraft.engines.length() > 0) {
    engine += aircraft.engines;
  }

  String engType = engineTypeName(
    aircraft.engineType
  );

  if (engType.length() > 0) {
    if (engine.length() > 0) {
      engine += " ";
    }

    engine += engType;
  }

  if (engine.length() == 0) {
    engine = "UNKNOWN";
  }

  printLine(0, centered(species));
  printLine(1, centered(engine));
}

void renderScreen4() {
  char line1[17];

  snprintf(
    line1,
    sizeof(line1),
    "DIST: %.1f km",
    aircraft.distance
  );

  String trend;

  if (distanceTrend < 0) {
    trend = "INCOMING";
  }
  else if (distanceTrend > 0) {
    trend = "OUTCOMING";
  }
  else {
    trend = "WAIT...";
  }

  printLine(0, centered(String(line1)));
  printLine(1, centered(trend));
}

void renderScreen5() {
  char line1[17];
  char line2[17];

  if (aircraft.speedValid) {
    int speedKt =
      (int)round(aircraft.speed);

    int speedKmh =
      (int)round(aircraft.speed * 1.852);

    snprintf(
      line1,
      sizeof(line1),
      "%dkm/h (%dkt)",
      speedKmh,
      speedKt
    );
  }
  else {
    snprintf(
      line1,
      sizeof(line1),
      "SPD: ---"
    );
  }

  if (aircraft.altitude > 0) {
    int flightLevel =
      (aircraft.altitude + 50) / 100;

    snprintf(
      line2,
      sizeof(line2),
      "FL%03d",
      flightLevel
    );
  }
  else {
    snprintf(
      line2,
      sizeof(line2),
      "FL---"
    );
  }

  printLine(0, centered(String(line1)));
  printLine(1, centered(String(line2)));
}

void renderScreen6() {
  int heading =
    ((int)round(aircraft.track)) % 360;

  int bearing =
    ((int)round(aircraft.bearing)) % 360;

  String hdgDir = cardinal(aircraft.track);
  String brgDir = cardinal(aircraft.bearing);

  char line1[17];
  char line2[17];

  snprintf(
    line1,
    sizeof(line1),
    "HDG %03d %s",
    heading,
    hdgDir.c_str()
  );

  snprintf(
    line2,
    sizeof(line2),
    "BRG %03d %s",
    bearing,
    brgDir.c_str()
  );

  printLine(0, centered(String(line1)));
  printLine(1, centered(String(line2)));
}

void renderScreen7() {
  String icao = safeText(
    aircraft.icao,
    "------"
  );

  String line1 = "ICAO: " + icao;

  char line2[17];

  if (aircraft.squawk >= 0) {
    snprintf(
      line2,
      sizeof(line2),
      "SQUAWK: %04d",
      aircraft.squawk
    );
  }
  else {
    snprintf(
      line2,
      sizeof(line2),
      "SQUAWK: ----"
    );
  }

  printLine(0, centered(line1));
  printLine(1, centered(String(line2)));
}

void renderScreen8() {
  printLine(
    0,
    centered("SAMOLOTY VRS")
  );

  printLine(
    1,
    centered(String(totalAircraftCount))
  );
}

void renderNoAircraftAnimation() {
  printLine(0, centered("ADS-B RADAR"));

  String line = "SKANOWANIE";

  for (uint8_t i = 0; i < (scanFrame % 4); i++) {
    line += ".";
  }

  printLine(1, centered(line));
  scanFrame++;
}

void renderCurrentScreen() {
  if (!aircraft.valid) {
    renderNoAircraftAnimation();
    return;
  }

  switch (currentScreen) {
    case 0: renderScreen1(); break;
    case 1: renderScreen2(); break;
    case 2: renderScreen3(); break;
    case 3: renderScreen4(); break;
    case 4: renderScreen5(); break;
    case 5: renderScreen6(); break;
    case 6: renderScreen7(); break;
    default: renderScreen8(); break;
  }
}

uint8_t enabledScreenCount() {
  uint8_t count = 0;

  for (uint8_t i = 0; i < 8; i++) {
    if (config.screenEnabled[i]) {
      count++;
    }
  }

  return count;
}

uint8_t nextEnabledScreen(uint8_t from) {
  for (uint8_t step = 1; step <= 8; step++) {
    uint8_t candidate =
      (from + step) % 8;

    if (config.screenEnabled[candidate]) {
      return candidate;
    }
  }

  return 0;
}

// =====================================================
// WWW HTML
// =====================================================

String htmlHeader(const String& title) {
  String html;

  html.reserve(1800);

  html += F("<!doctype html><html lang='pl'><head>");
  html += F("<meta charset='utf-8'>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += "<title>";
  html += title;
  html += F("</title>");

  html += F("<style>");
  html += F("*{box-sizing:border-box}");
  html += F("body{margin:0;background:#0b1220;color:#e5e7eb;font-family:Arial,sans-serif}");
  html += F("header{background:#111827;border-bottom:1px solid #263244;padding:16px 18px}");
  html += F("header strong{font-size:20px;color:#fff}");
  html += F("nav{margin-top:10px}");
  html += F("nav a{display:inline-block;margin-right:8px;color:#dbeafe;text-decoration:none;background:#1f2937;border:1px solid #374151;padding:8px 11px;border-radius:8px}");
  html += F("main{max-width:900px;margin:auto;padding:18px}");
  html += F(".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:14px}");
  html += F(".card{background:#111827;border:1px solid #263244;border-radius:12px;padding:16px;margin-bottom:14px}");
  html += F("h1,h2{margin-top:0;color:#fff}");
  html += F("h2{font-size:17px}");
  html += F(".big{font-size:26px;font-weight:bold;color:#fff}");
  html += F(".muted{color:#9ca3af;font-size:13px}");
  html += F(".row{display:flex;justify-content:space-between;gap:10px;padding:6px 0;border-bottom:1px solid #202b3a}");
  html += F(".row:last-child{border-bottom:0}");
  html += F(".badge{padding:4px 8px;border-radius:999px;font-size:12px;font-weight:bold}");
  html += F(".ok{background:#064e3b;color:#a7f3d0}.bad{background:#7f1d1d;color:#fecaca}");
  html += F("label{display:block;margin:12px 0 5px;font-size:13px;color:#cbd5e1}");
  html += F("input[type=text],input[type=number]{width:100%;padding:10px;border-radius:8px;border:1px solid #374151;background:#0f172a;color:#fff}");
  html += F("input[type=checkbox]{transform:scale(1.25);margin-right:9px}");
  html += F("button{background:#2563eb;border:0;color:#fff;font-weight:bold;padding:11px 16px;border-radius:8px;cursor:pointer}");
  html += F("button.danger{background:#991b1b}");
  html += F(".checks label{margin:10px 0}");
  html += F("code{color:#bfdbfe}");
  html += F("</style></head><body>");

  html += F("<header><strong>ADS-B Display</strong><nav>");
  html += F("<a href='/'>Status</a>");
  html += F("<a href='/settings'>Ustawienia</a>");
  html += F("</nav></header><main>");

  return html;
}

String htmlFooter() {
  return F("</main></body></html>");
}

String aircraftTrendText() {
  if (!aircraft.valid) return "-";
  if (distanceTrend < 0) return "INCOMING";
  if (distanceTrend > 0) return "OUTCOMING";
  return "WAIT";
}

void handleWebRoot() {
  String html = htmlHeader("ADS-B Display - Status");

  html += F("<div class='grid'>");

  html += F("<div class='card'><h2>Najblizszy samolot</h2>");

  if (aircraft.valid) {
    html += "<div class='big'>";
    html += htmlEscape(
      safeText(aircraft.model, aircraft.type)
    );
    html += F("</div>");

    html += "<div>";
    html += htmlEscape(
      safeText(aircraft.oper, "Nieznany operator")
    );
    html += F("</div><br>");

    html += F("<div class='row'><span>CALL</span><strong>");
    html += htmlEscape(safeText(aircraft.call));
    html += F("</strong></div>");

    html += F("<div class='row'><span>REG</span><strong>");
    html += htmlEscape(safeText(aircraft.reg));
    html += F("</strong></div>");

    html += F("<div class='row'><span>Odleglosc</span><strong>");
    html += String(aircraft.distance, 1);
    html += F(" km</strong></div>");

    html += F("<div class='row'><span>Ruch</span><strong>");
    html += aircraftTrendText();
    html += F("</strong></div>");

    html += F("<div class='row'><span>Predkosc</span><strong>");

    if (aircraft.speedValid) {
      html += String(
        (int)round(aircraft.speed * 1.852)
      );
      html += F(" km/h</strong></div>");
    } else {
      html += F("-</strong></div>");
    }
  }
  else {
    html += F("<div class='big'>Brak samolotu</div>");
  }

  html += F("</div>");

  html += F("<div class='card'><h2>System</h2>");

  html += F("<div class='row'><span>VRS</span>");
  html += boolBadge(lastVrsOk, "ONLINE", "OFFLINE");
  html += F("</div>");

  html += F("<div class='row'><span>Samoloty VRS</span><strong>");
  html += String(totalAircraftCount);
  html += F("</strong></div>");

  html += F("<div class='row'><span>LCD</span><strong>");
  html += backlightOn ? F("ON") : F("OFF");
  html += F("</strong></div>");

  html += F("<div class='row'><span>Czas PL</span><strong>");
  html += localTimeString();
  html += F("</strong></div>");

  html += F("<div class='row'><span>IP</span><strong>");
  html += WiFi.localIP().toString();
  html += F("</strong></div>");

  html += F("<div class='row'><span>WiFi</span><strong>");
  html += String(WiFi.RSSI());
  html += F(" dBm</strong></div>");

  html += F("<div class='row'><span>Heap</span><strong>");
  html += String(ESP.getFreeHeap());
  html += F(" B</strong></div>");

  html += F("</div></div>");

  html += F("<div class='card'><h2>Konfiguracja</h2>");
  html += F("<div class='row'><span>Promien</span><strong>");
  html += String(config.maxDistanceKm);
  html += F(" km</strong></div>");

  html += F("<div class='row'><span>Ekran</span><strong>");
  html += String(config.screenDurationSec);
  html += F(" s</strong></div>");

  html += F("<div class='row'><span>VRS refresh</span><strong>");
  html += String(config.dataRefreshSec);
  html += F(" s</strong></div>");

  html += F("<div class='row'><span>Noc</span><strong>");
  html += config.nightEnabled ? F("ON") : F("OFF");
  html += F("</strong></div>");

  html += F("<p><a href='/settings' style='color:#93c5fd'>Przejdz do ustawien &rarr;</a></p>");
  html += F("</div>");

  html += htmlFooter();

  webServer.send(200, "text/html; charset=utf-8", html);
}

String checkedAttr(bool checked) {
  return checked ? " checked" : "";
}

void handleWebSettings() {
  static const char* screenNames[8] = {
    "Model / Operator",
    "CALL / REG",
    "Species / Engines",
    "Distance / Direction",
    "Speed / Flight Level",
    "Heading / Bearing",
    "ICAO / Squawk",
    "Aircraft count"
  };

  String html = htmlHeader("ADS-B Display - Ustawienia");

  html += F("<form method='post' action='/save'>");

  html += F("<div class='card'><h2>Virtual Radar Server</h2>");

  html += F("<label>AircraftList.json URL</label><input type='text' name='vrs_url' value='");
  html += htmlEscape(config.vrsUrl);
  html += F("'>");

  html += F("<label>Szerokosc geograficzna odbiornika</label><input type='number' step='0.000001' name='lat' value='");
  html += String(config.rxLat, 6);
  html += F("'>");

  html += F("<label>Dlugosc geograficzna odbiornika</label><input type='number' step='0.000001' name='lon' value='");
  html += String(config.rxLon, 6);
  html += F("'>");

  html += F("<label>Maksymalny promien [km]</label><input type='number' min='1' max='500' name='max_dist' value='");
  html += String(config.maxDistanceKm);
  html += F("'>");

  html += F("<label>Odswiezanie danych VRS [s]</label><input type='number' min='1' max='60' name='refresh' value='");
  html += String(config.dataRefreshSec);
  html += F("'></div>");

  html += F("<div class='card'><h2>LCD</h2>");

  html += F("<label>Czas wyswietlania ekranu [s]</label><input type='number' min='1' max='30' name='screen_time' value='");
  html += String(config.screenDurationSec);
  html += F("'>");

  html += F("<label><input type='checkbox' name='night_enabled'");
  html += checkedAttr(config.nightEnabled);
  html += F("> Automatyczne wygaszanie nocne</label>");

  html += F("<label>Wylacz LCD o godzinie</label><input type='number' min='0' max='23' name='night_off' value='");
  html += String(config.nightOffHour);
  html += F("'>");

  html += F("<label>Wlacz LCD o godzinie</label><input type='number' min='0' max='23' name='night_on' value='");
  html += String(config.nightOnHour);
  html += F("'>");

  html += F("<h2 style='margin-top:22px'>Aktywne ekrany</h2><div class='checks'>");

  for (uint8_t i = 0; i < 8; i++) {
    html += F("<label><input type='checkbox' name='screen");
    html += String(i);
    html += "'";
    html += checkedAttr(config.screenEnabled[i]);
    html += "> ";
    html += String(i + 1);
    html += ". ";
    html += screenNames[i];
    html += F("</label>");
  }

  html += F("</div></div>");

  html += F("<div class='card'><h2>Alarm LPR</h2>");

  html += F("<label><input type='checkbox' name='lpr_enabled'");
  html += checkedAttr(config.lprAlertEnabled);
  html += F("> Miganie LCD gdy LPR zostanie nowym najblizszym</label>");

  html += F("<label>Liczba migniec</label><input type='number' min='1' max='10' name='lpr_blinks' value='");
  html += String(config.lprBlinks);
  html += F("'>");

  html += F("<label>Prog INCOMING / OUTCOMING [km]</label><input type='number' min='0.01' max='5' step='0.01' name='trend' value='");
  html += String(config.trendThresholdKm, 2);
  html += F("'></div>");

  html += F("<div class='card'><button type='submit'>ZAPISZ USTAWIENIA</button></div>");
  html += F("</form>");

  html += F("<form method='post' action='/reset' onsubmit=\"return confirm('Przywrocic ustawienia domyslne?');\">");
  html += F("<div class='card'><button type='submit' class='danger'>PRZYWROC DOMYSLNE</button></div></form>");

  html += htmlFooter();

  webServer.send(200, "text/html; charset=utf-8", html);
}

void handleWebSave() {
  if (webServer.hasArg("vrs_url")) {
    config.vrsUrl = webServer.arg("vrs_url");
    config.vrsUrl.trim();
  }

  if (webServer.hasArg("lat")) {
    config.rxLat = webServer.arg("lat").toFloat();
  }

  if (webServer.hasArg("lon")) {
    config.rxLon = webServer.arg("lon").toFloat();
  }

  if (webServer.hasArg("max_dist")) {
    config.maxDistanceKm =
      webServer.arg("max_dist").toInt();
  }

  if (webServer.hasArg("refresh")) {
    config.dataRefreshSec =
      webServer.arg("refresh").toInt();
  }

  if (webServer.hasArg("screen_time")) {
    config.screenDurationSec =
      webServer.arg("screen_time").toInt();
  }

  config.nightEnabled =
    webServer.hasArg("night_enabled");

  if (webServer.hasArg("night_off")) {
    config.nightOffHour =
      webServer.arg("night_off").toInt();
  }

  if (webServer.hasArg("night_on")) {
    config.nightOnHour =
      webServer.arg("night_on").toInt();
  }

  config.lprAlertEnabled =
    webServer.hasArg("lpr_enabled");

  if (webServer.hasArg("lpr_blinks")) {
    config.lprBlinks =
      webServer.arg("lpr_blinks").toInt();
  }

  if (webServer.hasArg("trend")) {
    config.trendThresholdKm =
      webServer.arg("trend").toFloat();
  }

  bool anyScreen = false;

  for (uint8_t i = 0; i < 8; i++) {
    String argName = "screen" + String(i);
    config.screenEnabled[i] = webServer.hasArg(argName);

    if (config.screenEnabled[i]) {
      anyScreen = true;
    }
  }

  if (!anyScreen) {
    config.screenEnabled[0] = true;
  }

  sanitizeConfig();
  saveConfig();

  currentScreen = 0;
  modelScrollOffset = 0;
  lastScreenChange = millis();
  lastDataRefresh = 0;

  applyBacklightSchedule(true);

  webServer.sendHeader("Location", "/settings");
  webServer.send(303, "text/plain", "Zapisano");
}

void handleWebReset() {
  setDefaultConfig();
  sanitizeConfig();
  saveConfig();

  currentScreen = 0;
  lastDataRefresh = 0;
  lastScreenChange = millis();

  applyBacklightSchedule(true);

  webServer.sendHeader("Location", "/settings");
  webServer.send(303, "text/plain", "Przywrocono");
}

void startWebServer() {
  if (webStarted) {
    return;
  }

  webServer.on("/", HTTP_GET, handleWebRoot);
  webServer.on("/settings", HTTP_GET, handleWebSettings);
  webServer.on("/save", HTTP_POST, handleWebSave);
  webServer.on("/reset", HTTP_POST, handleWebReset);

  webServer.onNotFound([]() {
    webServer.send(404, "text/plain", "404 - Not Found");
  });

  webServer.begin();
  webStarted = true;

  Serial.println(F("WWW: serwer HTTP uruchomiony"));
  Serial.print(F("WWW: http://"));
  Serial.print(WiFi.localIP());
  Serial.println('/');

  if (MDNS.begin("adsb-display")) {
    MDNS.addService("http", "tcp", 80);
    mdnsStarted = true;

    Serial.println(F("mDNS: http://adsb-display.local/"));
  }
  else {
    Serial.println(F("mDNS: start nieudany - uzyj adresu IP"));
  }
}

// =====================================================
// WIFI
// =====================================================

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print(F("Laczenie WiFi: "));
  Serial.println(WIFI_SSID);

  unsigned long start = millis();
  uint8_t dots = 0;

  while (WiFi.status() != WL_CONNECTED) {
    String line = "WiFi ";

    for (uint8_t i = 0; i < dots; i++) {
      line += ".";
    }

    printLine(0, centered("ADS-B RADAR"));
    printLine(1, centered(line));

    dots++;
    if (dots > 6) dots = 0;

    delay(350);

    if (millis() - start > 25000) {
      Serial.println(F("Brak WiFi."));
      printLine(0, centered("BRAK WIFI"));
      printLine(1, centered("PONAWIAM"));
      return false;
    }
  }

  Serial.println(F("WiFi OK"));
  Serial.print(F("IP: "));
  Serial.println(WiFi.localIP());

  printLine(0, centered("WIFI OK"));
  printLine(1, centered(WiFi.localIP().toString()));
  delay(800);

  if (!timeConfigured) {
    configureTimeNtp();
  }

  startWebServer();

  return true;
}

// =====================================================
// VRS
// =====================================================

void updateAircraftData() {
  if (WiFi.status() != WL_CONNECTED) {
    lastVrsOk = false;

    if (!connectWiFi()) {
      return;
    }
  }

  String url =
    config.vrsUrl +
    "?lat=" + String(config.rxLat, 6) +
    "&lng=" + String(config.rxLon, 6) +
    "&fDstU=" + String(config.maxDistanceKm);

  Serial.println();
  Serial.println(F("VRS GET:"));
  Serial.println(url);

  WiFiClient client;
  HTTPClient http;

  http.useHTTP10(true);
  http.setTimeout(6000);

  if (!http.begin(client, url)) {
    Serial.println(F("HTTP BEGIN ERROR"));
    lastVrsOk = false;
    lastVrsHttpCode = -1;
    return;
  }

  int httpCode = http.GET();

  lastVrsHttpCode = httpCode;

  Serial.print(F("HTTP: "));
  Serial.println(httpCode);

  if (httpCode != HTTP_CODE_OK) {
    lastVrsOk = false;
    http.end();
    return;
  }

  JsonDocument filter;

  filter["stm"] = true;
  filter["totalAc"] = true;

  filter["acList"][0]["Icao"] = true;
  filter["acList"][0]["Call"] = true;
  filter["acList"][0]["Reg"] = true;
  filter["acList"][0]["Type"] = true;
  filter["acList"][0]["Mdl"] = true;
  filter["acList"][0]["Op"] = true;
  filter["acList"][0]["OpCode"] = true;

  filter["acList"][0]["Species"] = true;
  filter["acList"][0]["Engines"] = true;
  filter["acList"][0]["EngType"] = true;

  filter["acList"][0]["Sqk"] = true;

  filter["acList"][0]["Alt"] = true;
  filter["acList"][0]["Spd"] = true;
  filter["acList"][0]["SpdTyp"] = true;
  filter["acList"][0]["Dst"] = true;
  filter["acList"][0]["Brng"] = true;
  filter["acList"][0]["Trak"] = true;
  filter["acList"][0]["TrkH"] = true;

  filter["acList"][0]["Bad"] = true;
  filter["acList"][0]["Gnd"] = true;
  filter["acList"][0]["PosStale"] = true;

  JsonDocument doc;

  DeserializationError error = deserializeJson(
    doc,
    http.getStream(),
    DeserializationOption::Filter(filter)
  );

  http.end();

  if (error) {
    Serial.print(F("JSON ERROR: "));
    Serial.println(error.c_str());
    lastVrsOk = false;
    return;
  }

  lastVrsOk = true;
  lastVrsSuccessMs = millis();

  if (!doc["stm"].isNull()) {
    uint64_t serverTimeMs =
      doc["stm"].as<uint64_t>();

    syncClockFromVrs(serverTimeMs);
    applyBacklightSchedule(false);
  }
  else {
    Serial.println(F("VRS TIME: brak pola stm w odpowiedzi"));
  }

  JsonArrayConst list =
    doc["acList"].as<JsonArrayConst>();

  int aircraftWithinRadius = list.size();

  totalAircraftCount =
    doc["totalAc"] | aircraftWithinRadius;

  Serial.print(F("AC w promieniu: "));
  Serial.println(aircraftWithinRadius);

  Serial.print(F("AC TOTAL VRS: "));
  Serial.println(totalAircraftCount);

  AircraftData nearest;
  float nearestDistance = 99999.0;

  for (JsonObjectConst a : list) {
    if (a["Bad"] | false) continue;
    if (a["PosStale"] | false) continue;
    if (a["Gnd"] | false) continue;
    if (a["Dst"].isNull()) continue;

    float dst = a["Dst"].as<float>();

    if (
      dst < 0 ||
      dst > config.maxDistanceKm
    ) {
      continue;
    }

    if (!nearest.valid || dst < nearestDistance) {
      nearest.valid = true;
      nearestDistance = dst;

      nearest.call = a["Call"] | "";
      nearest.reg = a["Reg"] | "";
      nearest.icao = a["Icao"] | "";
      nearest.type = a["Type"] | "";
      nearest.model = a["Mdl"] | "";
      nearest.oper = a["Op"] | "";
      nearest.opCode = a["OpCode"] | "";

      nearest.engines = a["Engines"] | "";
      nearest.engineType = a["EngType"] | 0;
      nearest.species = a["Species"] | 0;

      if (!a["Sqk"].isNull()) {
        nearest.squawk = a["Sqk"].as<int>();
      }
      else {
        nearest.squawk = -1;
      }

      nearest.distance = dst;
      nearest.bearing = a["Brng"] | 0.0f;
      nearest.track = a["Trak"] | 0.0f;
      nearest.trackIsHeading = a["TrkH"] | false;

      nearest.altitude = a["Alt"] | 0;

      if (!a["Spd"].isNull()) {
        nearest.speed = a["Spd"].as<float>();
        nearest.speedValid = true;
      }
      else {
        nearest.speed = 0.0;
        nearest.speedValid = false;
      }

      nearest.speedType = a["SpdTyp"] | 0;
    }
  }

  if (!nearest.valid) {
    aircraft.valid = false;
    previousAircraftKey = "";
    distanceTrend = 0;

    Serial.println(F("Brak samolotow."));
    return;
  }

  String newKey = aircraftKey(nearest.icao, nearest.call, nearest.reg);

  bool aircraftChanged =
    previousAircraftKey.length() > 0 &&
    newKey != previousAircraftKey;

  if (
    previousAircraftKey.length() > 0 &&
    newKey == previousAircraftKey
  ) {
    float delta =
      nearest.distance - previousDistance;

    if (delta < -config.trendThresholdKm) {
      distanceTrend = -1;
    }
    else if (delta > config.trendThresholdKm) {
      distanceTrend = 1;
    }
  }
  else {
    distanceTrend = 0;
  }

  if (previousAircraftKey.length() == 0) {
    aircraftChanged = true;
  }

  aircraft = nearest;

  previousAircraftKey = newKey;
  previousDistance = nearest.distance;

  Serial.println(F("--- NAJBLIZSZY ---"));

  Serial.print(F("MODEL: "));
  Serial.println(aircraft.model);

  Serial.print(F("OP: "));
  Serial.println(aircraft.oper);

  Serial.print(F("OP CODE: "));
  Serial.println(aircraft.opCode);

  Serial.print(F("CALL: "));
  Serial.println(aircraft.call);

  Serial.print(F("REG: "));
  Serial.println(aircraft.reg);

  Serial.print(F("DST: "));
  Serial.print(aircraft.distance, 1);
  Serial.println(F(" km"));

  Serial.print(F("SPD PRESENT: "));
  Serial.println(
    aircraft.speedValid ? F("YES") : F("NO")
  );

  if (aircraft.speedValid) {
    Serial.print(F("SPD: "));
    Serial.print(aircraft.speed, 1);
    Serial.print(F(" kt / "));
    Serial.print(
      (int)round(aircraft.speed * 1.852)
    );
    Serial.println(F(" km/h"));
  }

  Serial.print(F("TOTAL VRS: "));
  Serial.println(totalAircraftCount);

  if (aircraftChanged) {
    pendingLprAlert =
      config.lprAlertEnabled &&
      isLprAircraft(aircraft.call, aircraft.oper, aircraft.opCode);

    Serial.print(F("LPR MATCH: "));
    Serial.println(
      pendingLprAlert ? F("YES") : F("NO")
    );

    startNewAircraftAnimation();
  }
  else if (!newAircraftAnimationActive) {
    renderCurrentScreen();
  }
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(400);

  Serial.println();
  Serial.println(F("ADS-B LCD EXTENDED v14 WEB CONFIG"));

  setDefaultConfig();

  if (!LittleFS.begin()) {
    Serial.println(F("LittleFS: blad montowania"));
  }
  else {
    Serial.println(F("LittleFS: OK"));

    loadConfig();
    sanitizeConfig();
  }

  Wire.begin(
    I2C_SDA_GPIO,
    I2C_SCL_GPIO
  );

  lcd.init();
  lcd.backlight();
  backlightOn = true;
  lcd.clear();

  lcd.createChar(0, planeChar);

  startupAnimation();

  if (connectWiFi()) {
    if (!timeConfigured) {
      configureTimeNtp();
    }
  }

  updateAircraftData();
  applyBacklightSchedule(true);

  lastDataRefresh = millis();
  lastScreenChange = millis();
  lastBacklightCheck = millis();
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  unsigned long now = millis();

  if (webStarted) {
    webServer.handleClient();
  }

  if (mdnsStarted) {
    MDNS.update();
  }

  if (
    now - lastDataRefresh >=
    ((unsigned long)config.dataRefreshSec * 1000UL)
  ) {
    lastDataRefresh = now;
    updateAircraftData();
  }

  if (
    now - lastBacklightCheck >=
    BACKLIGHT_CHECK_MS
  ) {
    lastBacklightCheck = now;
    applyBacklightSchedule(false);
  }

  if (newAircraftAnimationActive) {
    updateNewAircraftAnimation();
    yield();
    return;
  }

  if (!aircraft.valid) {
    if (now - lastAnimationFrame >= 350) {
      lastAnimationFrame = now;
      renderNoAircraftAnimation();
    }

    yield();
    return;
  }

  if (
    now - lastScreenChange >=
    ((unsigned long)config.screenDurationSec * 1000UL)
  ) {
    lastScreenChange = now;

    currentScreen =
      nextEnabledScreen(currentScreen);

    lcd.clear();

    if (currentScreen == 0) {
      modelScrollOffset = 0;
      lastModelScroll = millis();
    }

    renderCurrentScreen();
  }

  if (currentScreen == 0) {
    renderModelLine(false);
  }

  yield();
}
