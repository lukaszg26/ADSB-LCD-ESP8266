// =====================================================

bool isLprAircraft(const String& callIn, const String& opIn, const String& opCodeIn) {
  String call = callIn;
  String op = opIn;
  String opCode = opCodeIn;
  call.toUpperCase(); op.toUpperCase(); opCode.toUpperCase();
  if (opCode == "LPR") return true;
  if (call.startsWith("LPR")) return true;
  if (op.indexOf("LOTNICZE POGOTOWIE RATUNKOWE") >= 0) return true;
  if (op.indexOf("POLISH MEDICAL AIR RESCUE") >= 0) return true;
  return false;
}

void runLprBlinkAlert() {
  Serial.println(F("*** LPR ALERT: LCD BLINK ***"));
  currentScreen = 0;
  lcd.clear();
  renderCurrentScreen();
  setBacklightState(true);
  delay(250);
  for (uint8_t i = 0; i < config.lprBlinks; i++) {
    lcd.noBacklight(); backlightOn = false; delay(250);
    lcd.backlight(); backlightOn = true; delay(450);
  }
  applyBacklightSchedule(true);
  lastScreenChange = millis();
}

void startupAnimation() {
  lcd.clear();
  printLine(0, centered("ADS-B RADAR"));
  for (uint8_t x = 0; x < 16; x++) {
    clearLine(1); lcd.setCursor(x, 1); lcd.write(byte(0)); delay(65); yield();
  }
  printLine(1, centered("START"));
  delay(500);
}

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  uint8_t dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    String line = "WiFi ";
    for (uint8_t i = 0; i < dots; i++) line += ".";
    printLine(0, centered("ADS-B RADAR")); printLine(1, centered(line));
    dots++; if (dots > 6) dots = 0; delay(350);
    if (millis() - start > 25000) {
      printLine(0, centered("BRAK WIFI")); printLine(1, centered("PONAWIAM")); return false;
    }
  }
  printLine(0, centered("WIFI OK")); printLine(1, centered(WiFi.localIP().toString())); delay(800);
  if (!timeConfigured) configureTimeNtp();
  if (!webStarted) startWebServer();
  if (config.mqttEnabled && !mqttClient.connected()) { lastMqttReconnectAttempt = 0; mqttConnect(); }
  return true;
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
  if (!newAircraftAnimationActive) return;
  if (millis() - newAircraftAnimationStart > 2000) {
    newAircraftAnimationActive = false;
    currentScreen = firstEnabledScreen();
    lastScreenChange = millis();
    lcd.clear(); renderCurrentScreen();
    if (pendingLprAlert) { pendingLprAlert = false; runLprBlinkAlert(); }
    return;
  }
  if (millis() - lastAnimationFrame < 110) return;
  lastAnimationFrame = millis();
  String name = centered(displayName(aircraft.call, aircraft.reg, aircraft.icao));
  clearLine(1);
  uint8_t planePos = newAircraftFrame % 16;
  lcd.setCursor(0, 1);
  for (uint8_t i = 0; i < 16; i++) {
    if (i == planePos) lcd.write(byte(0));
    else if (newAircraftFrame > 5) lcd.print(name[i]);
    else lcd.print(' ');
  }
  newAircraftFrame++;
}

void renderModelLine(bool resetScroll = false) {
  String model = safeText(aircraft.model, aircraft.type); model.trim();
  if (resetScroll) { modelScrollOffset = 0; lastModelScroll = millis(); }
  if (model.length() <= 16) { printLine(0, centered(model)); return; }
  String scrollText = model + "   ";
  if (millis() - lastModelScroll >= 350) {
    lastModelScroll = millis(); modelScrollOffset++;
    if (modelScrollOffset >= scrollText.length()) modelScrollOffset = 0;
  }
  String window = "";
  for (uint8_t i = 0; i < 16; i++) window += scrollText[(modelScrollOffset + i) % scrollText.length()];
  printLine(0, window);
}

String airportCodeFromVrs(String airportText) {
  airportText.trim(); if (!airportText.length()) return "";
  int separator = airportText.indexOf(' ');
  String code = separator > 0 ? airportText.substring(0, separator) : airportText;
  code.trim(); code.toUpperCase(); if (code.length() > 6) code = code.substring(0, 6);
  return code;
}

String buildRouteFromVrs(JsonObjectConst a) {
  String route = "";
  String from = airportCodeFromVrs(a["From"] | "");
  String to = airportCodeFromVrs(a["To"] | "");
  if (from.length()) route = from;
  JsonArrayConst stops = a["Stops"].as<JsonArrayConst>();
  if (!stops.isNull()) {
    for (JsonVariantConst stopValue : stops) {
      String stop = airportCodeFromVrs(stopValue.as<String>());
      if (!stop.length()) continue;
      if (route.length()) route += ">";
      route += stop;
    }
  }
  if (to.length()) { if (route.length()) route += ">"; route += to; }
  return route;
}

void renderRouteLine(bool resetScroll = false) {
  String route = aircraft.route; route.trim();
  if (resetScroll) { routeScrollOffset = 0; lastRouteScroll = millis(); }
  if (!route.length()) { printLine(1, centered("BRAK DANYCH")); return; }
  if (route.length() <= 16) { printLine(1, centered(route)); return; }
  String scrollText = route + "   ";
  if (millis() - lastRouteScroll >= 350) {
    lastRouteScroll = millis(); routeScrollOffset++;
    if (routeScrollOffset >= scrollText.length()) routeScrollOffset = 0;
  }
  String window = "";
  for (uint8_t i = 0; i < 16; i++) window += scrollText[(routeScrollOffset + i) % scrollText.length()];
  printLine(1, window);
}

void renderScreen1() {
  String op = safeText(aircraft.oper, "UNKNOWN OPERATOR");
  renderModelLine(true); printLine(1, centered(op));
}

void renderScreen2() {
  String call = safeText(aircraft.call, "NO CALLSIGN");
  String reg = safeText(aircraft.reg, "NO REG");
  printLine(0, centered("CALL: " + call));
  printLine(1, centered("REG: " + reg));
}

void renderScreen3() {
  String species = speciesName(aircraft.species);
  String engine = aircraft.engines;
  String engType = engineTypeName(aircraft.engineType);
  if (engType.length()) { if (engine.length()) engine += " "; engine += engType; }
  if (!engine.length()) engine = "UNKNOWN";
  printLine(0, centered(species)); printLine(1, centered(engine));
}

void renderScreen4() {
  char line1[17]; snprintf(line1, sizeof(line1), "DIST: %.1f km", aircraft.distance);
  String trend = distanceTrend < 0 ? "INCOMING" : (distanceTrend > 0 ? "OUTCOMING" : "WAIT...");
  printLine(0, centered(String(line1))); printLine(1, centered(trend));
}

void renderScreen5() {
  char line1[17], line2[17];
  if (aircraft.speedValid) {
    int speedKt = (int)round(aircraft.speed);
    int speedKmh = (int)round(aircraft.speed * 1.852);
    snprintf(line1, sizeof(line1), "%dkm/h (%dkt)", speedKmh, speedKt);
  } else snprintf(line1, sizeof(line1), "SPD: ---");
  if (aircraft.altitude > 0) snprintf(line2, sizeof(line2), "FL%03d", (aircraft.altitude + 50) / 100);
  else snprintf(line2, sizeof(line2), "FL---");
  printLine(0, centered(String(line1))); printLine(1, centered(String(line2)));
}

// =====================================================
// EKRAN 6
