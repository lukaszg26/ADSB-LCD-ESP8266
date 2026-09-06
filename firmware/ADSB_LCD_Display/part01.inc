/*
  ADS-B LCD 16x2 EXTENDED v16
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
  8. Trasa lotu (From / Stops / To)
  9. Liczba wszystkich samolotow VRS

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
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <PubSubClient.h>
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

  // Profile wyswietlania:
  // 0 CUSTOM, 1 NORMAL, 2 MINIMAL, 3 SPOTTER, 4 FULL
  uint8_t displayProfile;

  bool screenEnabled[9];

  // MQTT / Home Assistant
  bool mqttEnabled;
  String mqttHost;
  uint16_t mqttPort;
  String mqttUser;
  String mqttPassword;
  String mqttBaseTopic;
  String mqttDiscoveryPrefix;
};

AppConfig config;

const char* CONFIG_FILE = "/config.json";

// Sprawdzanie harmonogramu podswietlenia co 10 sekund.
const unsigned long BACKLIGHT_CHECK_MS = 10000;

// =====================================================
// SERWER WWW
// =====================================================

ESP8266WebServer webServer(80);
ESP8266HTTPUpdateServer httpUpdater;

// OTA przez WWW. Zmien haslo przed udostepnieniem urzadzenia w obcej sieci.
const char* OTA_USER = "admin";
const char* OTA_PASSWORD = "CHANGE_ME_NOW";

WiFiClient mqttNetClient;
PubSubClient mqttClient(mqttNetClient);

unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastMqttStatePublish = 0;
const unsigned long MQTT_RECONNECT_MS = 10000;
const unsigned long MQTT_STATE_MS = 5000;

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

  config.displayProfile = 1; // NORMAL

  for (uint8_t i = 0; i < 9; i++) {
    config.screenEnabled[i] = true;
  }

  config.mqttEnabled = false;
  config.mqttHost = "";
  config.mqttPort = 1883;
  config.mqttUser = "";
  config.mqttPassword = "";
  config.mqttBaseTopic = "adsb_display";
  config.mqttDiscoveryPrefix = "homeassistant";
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

  doc["display_profile"] = config.displayProfile;

  doc["mqtt_enabled"] = config.mqttEnabled;
  doc["mqtt_host"] = config.mqttHost;
  doc["mqtt_port"] = config.mqttPort;
  doc["mqtt_user"] = config.mqttUser;
  doc["mqtt_password"] = config.mqttPassword;
  doc["mqtt_base_topic"] = config.mqttBaseTopic;
  doc["mqtt_discovery_prefix"] = config.mqttDiscoveryPrefix;

  JsonArray screens = doc["screens"].to<JsonArray>();

  for (uint8_t i = 0; i < 9; i++) {
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

  if (!doc["vrs_url"].isNull()) {
    config.vrsUrl = doc["vrs_url"].as<String>();
  }

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
  config.displayProfile = doc["display_profile"] | config.displayProfile;
  config.mqttEnabled = doc["mqtt_enabled"] | config.mqttEnabled;

  if (!doc["mqtt_host"].isNull()) config.mqttHost = doc["mqtt_host"].as<String>();
  config.mqttPort = doc["mqtt_port"] | config.mqttPort;
  if (!doc["mqtt_user"].isNull()) config.mqttUser = doc["mqtt_user"].as<String>();
  if (!doc["mqtt_password"].isNull()) config.mqttPassword = doc["mqtt_password"].as<String>();
  if (!doc["mqtt_base_topic"].isNull()) config.mqttBaseTopic = doc["mqtt_base_topic"].as<String>();
  if (!doc["mqtt_discovery_prefix"].isNull()) config.mqttDiscoveryPrefix = doc["mqtt_discovery_prefix"].as<String>();

  JsonArrayConst screens = doc["screens"].as<JsonArrayConst>();

  if (!screens.isNull()) {
    if (screens.size() == 8) {
      for (uint8_t i = 0; i < 7; i++) config.screenEnabled[i] = screens[i].as<bool>();
      config.screenEnabled[7] = true;
      config.screenEnabled[8] = screens[7].as<bool>();
    } else {
      for (uint8_t i = 0; i < 9 && i < screens.size(); i++) config.screenEnabled[i] = screens[i].as<bool>();
    }
  }

  config.maxDistanceKm = constrain(config.maxDistanceKm, 5, 500);
  config.dataRefreshSec = constrain(config.dataRefreshSec, 1, 60);
  config.screenDurationSec = constrain(config.screenDurationSec, 2, 60);
  config.nightOffHour = constrain(config.nightOffHour, 0, 23);
  config.nightOnHour = constrain(config.nightOnHour, 0, 23);
  config.lprBlinks = constrain(config.lprBlinks, 1, 10);
  config.displayProfile = constrain(config.displayProfile, 0, 4);
  config.mqttPort = constrain(config.mqttPort, 1, 65535);

  config.mqttHost.trim();
  config.mqttUser.trim();
  config.mqttBaseTopic.trim();
  config.mqttDiscoveryPrefix.trim();
  if (config.mqttBaseTopic.length() == 0) config.mqttBaseTopic = "adsb_display";
  if (config.mqttDiscoveryPrefix.length() == 0) config.mqttDiscoveryPrefix = "homeassistant";

  if (config.trendThresholdKm < 0.01) config.trendThresholdKm = 0.01;
  if (config.trendThresholdKm > 5.0) config.trendThresholdKm = 5.0;

  bool anyScreen = false;
  for (uint8_t i = 0; i < 9; i++) {
    if (config.screenEnabled[i]) { anyScreen = true; break; }
  }
  if (!anyScreen) config.screenEnabled[0] = true;

  Serial.println(F("CONFIG: wczytano ustawienia"));
  return true;
}

// =====================================================
// LCD
// =====================================================

const uint8_t I2C_SDA_GPIO = 4;  // D2
const uint8_t I2C_SCL_GPIO = 5;  // D1
const uint8_t LCD_ADDRESS = 0x27;

LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);

// Aktualny obraz LCD do podgladu WWW / MQTT.
String lcdLine1 = "                ";
String lcdLine2 = "                ";

// -1 = AUTO (harmonogram), 0 = wymus OFF, 1 = wymus ON.
int8_t manualBacklightOverride = -1;

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
