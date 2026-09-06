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
  String route;

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

unsigned long lastDataRefresh = 0;
unsigned long lastScreenChange = 0;
unsigned long lastAnimationFrame = 0;
unsigned long lastModelScroll = 0;
uint16_t modelScrollOffset = 0;
unsigned long lastRouteScroll = 0;
uint16_t routeScrollOffset = 0;

uint8_t currentScreen = 0;
const uint8_t SCREEN_COUNT = 9;

bool newAircraftAnimationActive = false;
unsigned long newAircraftAnimationStart = 0;
uint8_t newAircraftFrame = 0;
uint8_t scanFrame = 0;

unsigned long lastBacklightCheck = 0;
bool backlightOn = true;
bool timeConfigured = false;
bool vrsTimeSynced = false;
unsigned long lastVrsTimeSyncMs = 0;
bool pendingLprAlert = false;

void renderCurrentScreen();
void startWebServer();
uint8_t firstEnabledScreen();
uint8_t nextEnabledScreen(uint8_t current);
bool isScreenActive(uint8_t index);
void mqttPublishState();
void mqttMaintain();

void printLine(uint8_t row, String text) {
  while (text.length() < 16) text += " ";
  if (text.length() > 16) text = text.substring(0, 16);
  if (row == 0) lcdLine1 = text;
  if (row == 1) lcdLine2 = text;
  lcd.setCursor(0, row);
  lcd.print(text);
}

void clearLine(uint8_t row) { printLine(row, ""); }

String centered(String text) {
  text.trim();
  if (text.length() > 16) text = text.substring(0, 16);
  String out = "";
  int spaces = 16 - text.length();
  int left = spaces / 2;
  for (int i = 0; i < left; i++) out += " ";
  out += text;
  while (out.length() < 16) out += " ";
  return out;
}

String safeText(String text, const String& fallback = "-") {
  text.trim();
  return text.length() ? text : fallback;
}

String cardinal(float degrees) {
  while (degrees < 0.0) degrees += 360.0;
  while (degrees >= 360.0) degrees -= 360.0;
  if (degrees >= 337.5 || degrees < 22.5) return "N";
  if (degrees < 67.5) return "NE";
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
  if (icao.length()) return icao;
  if (call.length()) return call;
  return reg;
}

String displayName(const String& call, const String& reg, const String& icao) {
  if (call.length()) return call;
  if (reg.length()) return reg;
  if (icao.length()) return icao;
  return "UNKNOWN";
}

void setBacklightState(bool on) {
  if (on == backlightOn) return;
  backlightOn = on;
  if (on) lcd.backlight(); else lcd.noBacklight();
}

void configureTimeNtp() {
  setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
  tzset();
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");
  timeConfigured = true;
}

int daysInMonthUtc(int year, int month) {
  static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (month == 2) {
    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    return leap ? 29 : 28;
  }
  return days[month - 1];
}

int dayOfWeekGregorian(int year, int month, int day) {
  static const int table[] = {0,3,2,5,0,3,5,1,4,6,2,4};
  if (month < 3) year -= 1;
  return (year + year/4 - year/100 + year/400 + table[month-1] + day) % 7;
}

int lastSundayOfMonthUtc(int year, int month) {
  int lastDay = daysInMonthUtc(year, month);
  return lastDay - dayOfWeekGregorian(year, month, lastDay);
}

bool isPolishDstUtc(time_t utc) {
  struct tm utcTm;
  gmtime_r(&utc, &utcTm);
  int year = utcTm.tm_year + 1900;
  int month = utcTm.tm_mon + 1;
  int day = utcTm.tm_mday;
  int hour = utcTm.tm_hour;
  if (month < 3 || month > 10) return false;
  if (month > 3 && month < 10) return true;
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

int polishUtcOffsetSeconds(time_t utc) { return isPolishDstUtc(utc) ? 7200 : 3600; }

bool getPolishLocalTime(struct tm &localTm) {
  time_t utc = time(nullptr);
  if (utc < 1700000000) return false;
  time_t polishTime = utc + polishUtcOffsetSeconds(utc);
  gmtime_r(&polishTime, &localTm);
  return true;
}

void syncClockFromVrs(uint64_t serverTimeMs) {
  if (serverTimeMs < 1700000000000ULL) return;
  struct timeval tv;
  tv.tv_sec = (time_t)(serverTimeMs / 1000ULL);
  tv.tv_usec = (suseconds_t)((serverTimeMs % 1000ULL) * 1000ULL);
  settimeofday(&tv, nullptr);
  vrsTimeSynced = true;
  lastVrsTimeSyncMs = millis();
}

bool getLocalHour(int &hourOut) {
  struct tm localTm;
  if (!getPolishLocalTime(localTm)) return false;
  hourOut = localTm.tm_hour;
  return true;
}

bool isNightDisplayTime() {
  int hour;
  if (!getLocalHour(hour)) return false;
  if (!config.nightEnabled) return false;
  return (hour >= config.nightOffHour || hour < config.nightOnHour);
}

void applyBacklightSchedule(bool forceLog = false) {
  bool shouldBeOn = !isNightDisplayTime();
  if (manualBacklightOverride == 0) shouldBeOn = false;
  if (manualBacklightOverride == 1) shouldBeOn = true;
  setBacklightState(shouldBeOn);
}

// =====================================================
// ROZPOZNAWANIE LPR
