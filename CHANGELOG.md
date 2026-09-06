# Changelog

W tym pliku dokumentowane są najważniejsze zmiany projektu.

## [v16.0] - 2026-09-06

### Dodano

- podgląd LCD 16×2 na żywo w panelu WWW
- sterowanie WWW: następny ekran, LCD AUTO/ON/OFF, test LPR i restart ESP
- OTA firmware przez przeglądarkę pod `/update`
- MQTT przez bibliotekę `PubSubClient`
- Home Assistant MQTT Discovery
- encje Home Assistant dla danych samolotu, VRS i LCD
- sterowanie profilem wyświetlania i trybem LCD z Home Assistant
- profile: `CUSTOM`, `NORMAL`, `MINIMAL`, `SPOTTER`, `FULL`
- obsługę 9 ekranów LCD
- ekran `TRASA` z wykorzystaniem `From`, `Stops`, `To`
- przewijanie długiej trasy

### Zmieniono

- panel WWW rozszerzono o konfigurację MQTT i profili
- aktualny firmware docelowo wskazuje na v16
- publiczna wersja kodu używa placeholderów Wi‑Fi i przykładowych danych VRS

### Zależności

- dodano `PubSubClient by Nick O'Leary`

## [v15.0] - 2026-09-05

### Dodano

- ekran trasy lotu
- pobieranie pól VRS `From`, `Stops`, `To`
- ekran trasy dostępny w ustawieniach WWW
- przewijanie długiej trasy
- fallback `BRAK DANYCH`, gdy VRS nie zna trasy

## [v14.1] - 2026-09-03

### Dodano

- wbudowany panel konfiguracji WWW
- zapis konfiguracji do LittleFS
- konfigurację URL VRS, pozycji odbiornika i promienia
- konfigurację czasu ekranów
- konfigurację nocnego wygaszania
- konfigurację alertu LPR
- indywidualne włączanie/wyłączanie ekranów
- mDNS `adsb-display.local`

### Naprawiono

- problem kompilacji Arduino związany z automatycznie generowanymi prototypami i typem `AircraftData`

### Funkcje bazowe

- wybór najbliższego samolotu z VRS
- model/operator, callsign/registration, species/engines
- `INCOMING / OUTCOMING`
- prędkość km/h i kt, Flight Level
- heading/track i bearing
- ICAO i squawk
- liczba wszystkich samolotów VRS
- animacja `NOWY NAJBLIZSZY`
- alert LPR
- wygaszanie LCD 21:00–06:00
- obsługa polskiego CET/CEST na podstawie `stm`

---

Starsza stabilna wersja v14.1 jest dostępna w `firmware/releases/v14.1/` oraz na gałęzi `v14.1-stable`.