#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <nvs_flash.h> // Do obsługi formatowania NVS przy błędzie
#include <time.h>
#include <Dusk2Dawn.h>
#include <WiFiManager.h>
#include <Ticker.h> // Dodano do sprzętowego migania LED w tle
#include <esp_sntp.h> // Do sprawdzania statusu synchronizacji NTP
#include <esp_system.h> // Do obsługi shutdown handler
#include <esp_task_wdt.h> // Do obsługi sprzętowego watchdoga
#include <Update.h> // Dodano do obsługi OTA
#include <esp_timer.h> // Do precyzyjnego liczenia uptime (odporne na przepełnienie po 49 dniach)
#include <inttypes.h> // Do przenośnego formatowania uint32_t
#include "html_templates.h"

// --- BEZPIECZEŃSTWO ---
const char* ota_user = "admin";
const char* ota_pass = "astro123"; // ZMIEŃ NA WŁASNE, BEZPIECZNE HASŁO!

// --- WERSJA OPROGRAMOWANIA ---
const char* APP_VERSION = "1.0";
#ifndef BUILD_NUMBER
#define BUILD_NUMBER 0
#endif

// --- KONFIGURACJA SPRZĘTOWA ---
const int OUTPUT_PIN = 5; // Wyjście na EL817 (Satel Integra)
const int LED_PIN = 8;    // Wbudowana dioda LED w C3 Supermini
const int BUTTON_PIN = 9; // Przycisk BOOT

WebServer server(80);
Preferences preferences;
Ticker ticker; // Obiekt timera w tle

// Zmienne ustawień
double userLat;
double userLng;
int offsetSunrise;
int offsetSunset;
bool invertOutput;
int currentMode; // 0=AUTO, 1=ON, 2=OFF

// Zmienne do przechowywania aktualnego wschodu/zachodu do wyświetlania na WWW
int currentSunrise = -1;
int currentSunset = -1;

// Zmienne do obsługi długoterminowej stabilności
bool settingsDirty = false; // Flaga "brudnych" ustawień do rzadkiego zapisu
unsigned long lastSaveTime = 0; // Czas ostatniego zapisu do NVS
bool g_timeIsFromNVS = false; // Flaga informująca, czy czas został przywrócony z NVS
uint32_t bootCount = 0; // Licznik restartów urządzenia
uint64_t recordUptime = 0; // Rekordowy czas pracy urządzenia (w sekundach)

// Timery
unsigned long lastCheckTime = 0;
const unsigned long checkInterval = 60000;
unsigned long lastWiFiCheck = 0;
const unsigned long wifiCheckInterval = 30000;
unsigned long wifiDisconnectStartTime = 0;
unsigned long lastHeapCheck = 0;
unsigned long lastNTPSync = 0;
unsigned long lastServerRestart = 0;
bool isWifiDisconnected = false; // Flaga do obsługi watchdoga WiFi, pewniejsza niż sentinel = 0

// Zmienna do obsługi stałego błysku 200ms
unsigned long lastBlinkStart = 0;

// Obsługa przycisku
unsigned long buttonDownTime = 0;
unsigned long buttonPressTime = 0;
bool isButtonPressed = false;
bool apMode = false; // Flaga trybu AP dla synchronizacji LED

// --- DEKLARACJE FUNKCJI ---
void loadSettings();
void updateRelayState();
void updateLED();
void handleRoot();
void setMode(int mode);
void setupWebServer();
void safeRestart();

// Funkcja wywoływana przez Ticker w tle (dla 5Hz)
void tick() {
  digitalWrite(LED_PIN, !digitalRead(LED_PIN));
}

// Funkcja wywoływana tuż przed restartem (również przy panic), aby ustawić wyjście w stan bezpieczny
void shutdown_handler() {
    pinMode(OUTPUT_PIN, OUTPUT); // Upewnij się, że pin jest skonfigurowany
    digitalWrite(OUTPUT_PIN, LOW); // Ustaw bezpieczny stan (niski)
}

void loadSettings() {
  if (!preferences.begin("astro", false)) {
    if (Serial) Serial.println("Błąd odczytu NVS. Próba awaryjnego formatowania partycji...");

    // Sformatuj partycję NVS
    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK && Serial) {
      Serial.printf("Błąd krytyczny: nie można sformatować NVS: %s\n", esp_err_to_name(err));
    }

    // Zainicjuj sformatowaną partycję
    err = nvs_flash_init();
    if (err != ESP_OK && Serial) {
      Serial.printf("Błąd krytyczny: nie można zainicjowac NVS: %s\n", esp_err_to_name(err));
    }
    
    if (Serial) Serial.println("Formatowanie NVS zakończone. Zapisuję wartości domyślne.");

    // Spróbuj ponownie otworzyć i od razu zapisz wartości domyślne
    if (!preferences.begin("astro", false)) {
      if (Serial) Serial.println("Krytyczny błąd: NVS trwale uszkodzony. Używam bezpiecznych wartości w RAM.");
      // Ustawienia awaryjne (tylko RAM) - urządzenie będzie działać do czasu wyłączenia zasilania
      userLat = 52.069;
      userLng = 19.480;
      offsetSunrise = 0;
      offsetSunset = 0;
      invertOutput = false;
      currentMode = 0;
      bootCount = 1;
      return; // BEZWZGLĘDNIE przerwij funkcję, brak dalszych prób odczytu uszkodzonej pamięci
    }
    
    // Jeśli formatowanie pomogło - zapisz wartości domyślne
    preferences.putDouble("lat", 52.069);
    preferences.putDouble("lng", 19.480);
    preferences.putInt("osr", 0);
    preferences.putInt("oss", 0);
    preferences.putBool("inv", false);
    preferences.putInt("mode", 0);
    preferences.putUInt("boots", 0); // Zainicjuj licznik bootowań
    preferences.putULong64("recUpt", 0); // Zainicjuj rekord uptime
  }

  // Odczytaj ustawienia (istniejące lub domyślne po formacie)
  userLat = preferences.getDouble("lat", 52.069);
  userLng = preferences.getDouble("lng", 19.480);
  offsetSunrise = preferences.getInt("osr", 0);
  offsetSunset = preferences.getInt("oss", 0);
  invertOutput = preferences.getBool("inv", false);
  currentMode = preferences.getInt("mode", 0);

  // Inkrementacja i zapis licznika bootowań
  bootCount = preferences.getUInt("boots", 0) + 1;
  preferences.putUInt("boots", bootCount);
  recordUptime = preferences.getULong64("recUpt", 0);
  if (Serial) Serial.printf("Boot #%" PRIu32 "\n", bootCount);

  // Odtwarzanie czasu po restarcie (w przypadku braku dostępu do serwera NTP)
  int64_t lastTime = preferences.getLong64("lastTime", 0);
  if (time(nullptr) < 1000000000l && lastTime > 1000000000l) {
    struct timeval tv;
    tv.tv_sec = (time_t)(lastTime + 2); // +2 sekundy na zrekompensowanie czasu bootowania
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
    g_timeIsFromNVS = true; // Ustaw flagę, że czas jest z pamięci
  }
  preferences.end();
}

void safeRestart() {
  time_t now = time(nullptr);
  preferences.begin("astro", false);
  if (now > 1000000000l) {
    preferences.putLong64("lastTime", (int64_t)now);
  }
  uint64_t currentUptime = esp_timer_get_time() / 1000000ULL;
  if (currentUptime > recordUptime) {
    preferences.putULong64("recUpt", currentUptime);
  }
  preferences.end();
  ESP.restart();
}

void updateRelayState() {
  static int lastCalculatedOutputState = -1; // -1 = nieznany
  int desiredState;

  if (currentMode == 1) {
    desiredState = HIGH;
  } else if (currentMode == 2) {
    desiredState = LOW;
  } else {
    time_t now = time(nullptr);
    if (now < 1000000000l) return; // Nie zmieniaj stanu, jeśli czas nie jest zsynchronizowany

    struct tm tinfo_utc;
    gmtime_r(&now, &tinfo_utc);

    Dusk2Dawn loc(userLat, userLng, 0);
    int sr = loc.sunrise(tinfo_utc.tm_year + 1900, tinfo_utc.tm_mon + 1, tinfo_utc.tm_mday, false);
    int ss = loc.sunset(tinfo_utc.tm_year + 1900, tinfo_utc.tm_mon + 1, tinfo_utc.tm_mday, false);

    sr = (sr + offsetSunrise) % 1440; if (sr < 0) sr += 1440;
    ss = (ss + offsetSunset) % 1440; if (ss < 0) ss += 1440;

    currentSunrise = sr;
    currentSunset = ss;

    int cur = tinfo_utc.tm_hour * 60 + tinfo_utc.tm_min;
    bool isNight = (sr < ss) ? (cur < sr || cur > ss) : (cur > ss || cur < sr);
    desiredState = (invertOutput ? !isNight : isNight) ? HIGH : LOW;
  }

  // Zapisz stan na wyjście tylko wtedy, gdy się zmienił, aby oszczędzać przekaźnik
  if (desiredState != lastCalculatedOutputState) {
    digitalWrite(OUTPUT_PIN, desiredState);
    lastCalculatedOutputState = desiredState;
    // Nie ustawiamy `settingsDirty`, aby nie zapisywać do NVS przy każdej zmianie stanu przekaźnika
  }
}

void updateLED() {
  if (apMode) return; // W trybie AP, Ticker zarządza diodą
  unsigned long cm = millis();
  unsigned long period = 0;

  // Ustalanie okresu migania (częstotliwości)
  if (WiFi.status() != WL_CONNECTED) {
    period = 500;  // 2Hz
  } else if (time(nullptr) < 1000000000l) {
    period = 1000; // 1Hz
  } else if (digitalRead(OUTPUT_PIN) == LOW) {
    period = 2000; // 0.5Hz
  } else {
    digitalWrite(LED_PIN, LOW); // Świeci ciągle (Active Low)
    return;
  }

  // Resetujemy licznik cyklu, gdy upłynie czas okresu
  if (cm - lastBlinkStart >= period) {
    lastBlinkStart = cm;
  }

  // Generowanie STAŁEGO błysku o długości 200ms
  // LED_PIN w trybie LOW oznacza, że dioda świeci.
  if (cm - lastBlinkStart < 200) {
    digitalWrite(LED_PIN, LOW);  // Włączona przez pierwsze 200ms cyklu
  } else {
    digitalWrite(LED_PIN, HIGH); // Wyłączona przez resztę czasu
  }
}

// --- Funkcja pomocnicza do formatowania uptime ---
void formatUptime(uint64_t uptime_sec, char* buf, size_t bufSize) {
  int u_years = uptime_sec / 31536000ULL; // 365 dni
  int u_months = (uptime_sec % 31536000ULL) / 2592000ULL; // 30 dni
  int u_days = (uptime_sec % 2592000ULL) / 86400ULL;
  int u_hours = (uptime_sec % 86400ULL) / 3600ULL;
  int u_mins = (uptime_sec % 3600ULL) / 60ULL;
  int u_secs = uptime_sec % 60ULL;

  if (u_years > 0) {
    snprintf(buf, bufSize, "%d lat, %d mies., %d dni, %d godz., %d min., %d sek.", u_years, u_months, u_days, u_hours, u_mins, u_secs);
  } else if (u_months > 0) {
    snprintf(buf, bufSize, "%d mies., %d dni, %d godz., %d min., %d sek.", u_months, u_days, u_hours, u_mins, u_secs);
  } else if (u_days > 0) {
    snprintf(buf, bufSize, "%d dni, %d godz., %d min., %d sek.", u_days, u_hours, u_mins, u_secs);
  } else {
    snprintf(buf, bufSize, "%d godz., %d min., %d sek.", u_hours, u_mins, u_secs);
  }
}

void handleRoot() {
  // --- Pre-kalkulacja wszystkich dynamicznych wartości, aby uniknąć alokacji String w pętli ---
  char lat_str[10], lng_str[10], osr_str[5], oss_str[5];
  dtostrf(userLat, 0, 5, lat_str);
  dtostrf(userLng, 0, 5, lng_str);
  itoa(offsetSunrise, osr_str, 10);
  itoa(offsetSunset, oss_str, 10);
  const char* inv_str = invertOutput ? "checked" : "";
  const char* pin_state_str = (digitalRead(OUTPUT_PIN) == HIGH) ? "WYSOKI (AKTYWNY)" : "NISKI (NIEAKTYWNY)";

  char onTimeStr[6] = "--:--";
  char offTimeStr[6] = "--:--";
  const char* modeText;
  const char* badgeClass;
  
  time_t now = time(nullptr);
  if (currentSunrise >= 0 && currentSunset >= 0 && now > 1000000000l) {
    struct tm tinfo_local;
    struct tm utc_tm;
    localtime_r(&now, &tinfo_local);
    gmtime_r(&now, &utc_tm);
    int tz_offset = difftime(mktime(&tinfo_local), mktime(&utc_tm)) / 60;

    int onTime = invertOutput ? currentSunrise : currentSunset;
    int offTime = invertOutput ? currentSunset : currentSunrise;
    
    onTime = (onTime + tz_offset) % 1440; if (onTime < 0) onTime += 1440;
    offTime = (offTime + tz_offset) % 1440; if (offTime < 0) offTime += 1440;

    snprintf(onTimeStr, sizeof(onTimeStr), "%02d:%02d", onTime / 60, onTime % 60);
    snprintf(offTimeStr, sizeof(offTimeStr), "%02d:%02d", offTime / 60, offTime % 60);
  }

  if (currentMode == 0) {
    if (now > 1000000000l) { modeText = "AUTO (Astro)"; badgeClass = "badge-auto"; }
    else { modeText = "AUTO (Brak NTP)"; badgeClass = "badge-warning"; }
  } else if (currentMode == 1) {
    modeText = "WYMUSZONY ON"; badgeClass = "badge-on";
  } else { // currentMode == 2
    modeText = "WYMUSZONY OFF"; badgeClass = "badge-off";
  }
  
  char version_buf[64];
  snprintf(version_buf, sizeof(version_buf), "%s.%d (%s %s)", APP_VERSION, BUILD_NUMBER, __DATE__, __TIME__);

  char uptime_str[128];
  uint64_t currentUptime = esp_timer_get_time() / 1000000ULL;
  formatUptime(currentUptime, uptime_str, sizeof(uptime_str));

  char rec_uptime_str[128];
  uint64_t displayRecUptime = (currentUptime > recordUptime) ? currentUptime : recordUptime;
  formatUptime(displayRecUptime, rec_uptime_str, sizeof(rec_uptime_str));

  // --- Wysyłanie odpowiedzi w kawałkach (chunked) w celu oszczędzania RAM ---
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  const char* template_ptr = htmlTemplate;
  const char* placeholder_start;
  
  while ((placeholder_start = strstr_P(template_ptr, "{{")) != NULL) {
    // Wyślij zawartość przed placeholderem
    server.sendContent_P(template_ptr, placeholder_start - template_ptr);

    const char* placeholder_end = strstr_P(placeholder_start + 2, "}}");
    if (placeholder_end == NULL) { // Błędny format, wyślij resztę i zakończ
      server.sendContent_P(placeholder_start);
      template_ptr = NULL;
      break;
    }

    // Wyodrębnij nazwę placeholdera
    int name_len = placeholder_end - (placeholder_start + 2);
    char name_buf[32];
    if (name_len >= sizeof(name_buf)) name_len = sizeof(name_buf) - 1;
    strncpy_P(name_buf, placeholder_start + 2, name_len);
    name_buf[name_len] = '\0';

    // --- Wyślij wartość zastępczą ---
    if (strcmp(name_buf, "TIME_STATUS_MSG") == 0) {
      if (g_timeIsFromNVS) {
        static const char nvs_warn[] PROGMEM = "<div class='alert alert-warning'><strong>&#9888; Tryb awaryjny:</strong> Czas przywrócony z pamięci.<br><button onclick=\"fetch('/api/settime', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'time=' + Math.floor(Date.now() / 1000) }).then(response => { if (response.ok) { location.reload(); } else { alert('Błąd synchronizacji czasu!'); } })\" style='margin-top:8px;padding:5px 10px;background:#28a745;color:white;border:none;border-radius:3px;cursor:pointer;'>&#128260; Zsynchronizuj z tego urządzenia</button></div>";
        server.sendContent_P(nvs_warn);
      }
    } else if (strcmp(name_buf, "BADGE_CLASS") == 0) {
      server.sendContent(badgeClass);
    } else if (strcmp(name_buf, "MODE_TEXT") == 0) {
      server.sendContent(modeText);
    } else if (strcmp(name_buf, "PIN_STATE") == 0) {
      server.sendContent(pin_state_str);
    } else if (strcmp(name_buf, "ON_TIME") == 0) {
      server.sendContent(onTimeStr);
    } else if (strcmp(name_buf, "OFF_TIME") == 0) {
      server.sendContent(offTimeStr);
    } else if (strcmp(name_buf, "LAT") == 0) {
      server.sendContent(lat_str);
    } else if (strcmp(name_buf, "LNG") == 0) {
      server.sendContent(lng_str);
    } else if (strcmp(name_buf, "OSR") == 0) {
      server.sendContent(osr_str);
    } else if (strcmp(name_buf, "OSS") == 0) {
      server.sendContent(oss_str);
    } else if (strcmp(name_buf, "INV") == 0) {
      if (inv_str[0] != '\0') { // Zapobiega wysłaniu pustego stringa (co kończy odpowiedź chunked)
        server.sendContent(inv_str);
      }
    } else if (strcmp(name_buf, "VERSION") == 0) {
      server.sendContent(version_buf);
    } else if (strcmp(name_buf, "UPTIME") == 0) {
      server.sendContent(uptime_str);
    } else if (strcmp(name_buf, "REC_UPTIME") == 0) {
      server.sendContent(rec_uptime_str);
    } else {
      // Nieznany placeholder, wyślij go dosłownie
      server.sendContent_P(placeholder_start, (placeholder_end - placeholder_start) + 2);
    }
    
    template_ptr = placeholder_end + 2;
  }

  // Wyślij pozostałą część szablonu
  if (template_ptr != NULL) {
    server.sendContent_P(template_ptr);
  }

  server.sendContent(""); // Zakończ odpowiedź
}

void setMode(int m) {
  if (currentMode != m) {
    currentMode = m;
    settingsDirty = true; // Oznacz ustawienia jako "brudne", ale nie zapisuj od razu
  }
  updateRelayState();
  server.sendHeader("Location", "/");
  server.send(303);
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  
  server.on("/save", HTTP_POST, [](){
    double lat = server.arg("lat").toDouble();
    double lng = server.arg("lng").toDouble();

    // Walidacja współrzędnych
    if (lat < -90.0 || lat > 90.0 || lng < -180.0 || lng > 180.0) {
      server.send(400, "text/plain", "Nieprawidłowe współrzędne geograficzne.");
      return;
    }

    userLat = lat; userLng = lng;
    offsetSunrise = server.arg("osr").toInt(); offsetSunset = server.arg("oss").toInt();
    invertOutput = server.hasArg("inv");
    preferences.begin("astro", false);
    preferences.putDouble("lat", userLat); preferences.putDouble("lng", userLng);
    preferences.putInt("osr", offsetSunrise); preferences.putInt("oss", offsetSunset);
    preferences.putBool("inv", invertOutput); preferences.end();
    updateRelayState();
    server.sendHeader("Location", "/"); server.send(303);
  });

  server.on("/api/auto", HTTP_GET, [](){ setMode(0); });
  server.on("/api/on", HTTP_GET, [](){ setMode(1); });
  server.on("/api/off", HTTP_GET, [](){ setMode(2); });
  server.on("/api/resetwifi", HTTP_GET, [](){
    server.send(200, "text/plain", "Reset WiFi...");
    delay(500); WiFiManager wm; wm.resetSettings(); safeRestart();
  });

  server.on("/api/settime", HTTP_POST, [](){
    // Endpoint do ręcznej synchronizacji czasu.
    // Opcjonalnie można dodać autentykację, aby zabezpieczyć ten endpoint:
    // if (!server.authenticate(ota_user, ota_pass)) { return server.requestAuthentication(); }

    if (server.hasArg("time")) {
      // Poprawka: Użycie atoll() zamiast toLongLong() dla lepszej kompatybilności ze starszymi wersjami frameworka.
      long long newTime = atoll(server.arg("time").c_str());
      // Walidacja, czy czas jest sensowny (np. między 2020 a 2100 rokiem)
      if (newTime > 1000000000LL && newTime < 4102444800LL) {
        struct timeval tv = {(time_t)newTime, 0};
        settimeofday(&tv, NULL);
        g_timeIsFromNVS = false; // Czas jest teraz ręcznie zsynchronizowany
        // Zapisz od razu do NVS, aby przetrwać ewentualny restart
        preferences.begin("astro", false);
        preferences.putLong64("lastTime", (int64_t)newTime);
        preferences.end();
        server.send(200, "text/plain", "OK");
        return;
      }
    }
    server.send(400, "text/plain", "Invalid time");
  });

  // Endpoint API do zdalnej diagnostyki
  server.on("/api/status", HTTP_GET, [](){
    uint64_t currentUptime = esp_timer_get_time() / 1000000ULL;
    uint64_t displayRecUptime = (currentUptime > recordUptime) ? currentUptime : recordUptime;
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"heap\":%u,\"uptime\":%" PRIu64 ",\"record_uptime\":%" PRIu64 ",\"boots\":%" PRIu32 ",\"wifi_connected\":%d,\"time_synced\":%d,\"mode\":%d}",
        ESP.getFreeHeap(), currentUptime, displayRecUptime, bootCount,
        WiFi.status() == WL_CONNECTED,
        time(nullptr) > 1000000000l,
        currentMode);
    server.send(200, "application/json", buf);
  });


  // --- OBSŁUGA OTA ---
  server.on("/update", HTTP_GET, []() {
    if (!server.authenticate(ota_user, ota_pass)) {
      return server.requestAuthentication();
    }
    server.sendHeader("Connection", "close");
    char version_buf[64];
    snprintf(version_buf, sizeof(version_buf), "%s.%d (%s %s)", APP_VERSION, BUILD_NUMBER, __DATE__, __TIME__);
    
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");

    const char* template_ptr = otaHtmlTemplate;
    const char* placeholder_start;
    
    while ((placeholder_start = strstr_P(template_ptr, "{{")) != NULL) {
      server.sendContent_P(template_ptr, placeholder_start - template_ptr);

      const char* placeholder_end = strstr_P(placeholder_start + 2, "}}");
      if (placeholder_end == NULL) { 
        server.sendContent_P(placeholder_start);
        template_ptr = NULL;
        break;
      }

      int name_len = placeholder_end - (placeholder_start + 2);
      char name_buf[32];
      if (name_len >= sizeof(name_buf)) name_len = sizeof(name_buf) - 1;
      strncpy_P(name_buf, placeholder_start + 2, name_len);
      name_buf[name_len] = '\0';

      if (strcmp(name_buf, "VERSION") == 0) server.sendContent(version_buf);
      else server.sendContent_P(placeholder_start, (placeholder_end - placeholder_start) + 2);
      
      template_ptr = placeholder_end + 2;
    }
    if (template_ptr != NULL) server.sendContent_P(template_ptr);
    server.sendContent("");
  });

  server.on("/update", HTTP_POST, []() {
    if (!server.authenticate(ota_user, ota_pass)) {
      return server.requestAuthentication();
    }
    server.sendHeader("Connection", "close");
    if (Update.hasError()) {
      server.send(500, "text/plain", "Błąd aktualizacji!");
      // Jeśli aktualizacja się nie powiodła, musimy ponownie włączyć Watchdoga,
      // ponieważ nie nastąpi restart urządzenia.
      if (Serial) Serial.println("Błąd OTA. Przywracam Watchdoga...");
      esp_task_wdt_add(NULL);
    } else {
      server.send(200, "text/plain", "Aktualizacja udana. Restart urządzenia...");
      delay(100); // Daj klientowi chwilę na odebranie odpowiedzi
      safeRestart();
    }
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      // Dodano autentykację na początku uploadu, aby zablokować nieautoryzowane próby.
      // Sprawdzamy to tutaj, a nie tylko w handlerze POST, aby uniknąć rozpoczęcia
      // zapisu do flash, jeśli żądanie jest nieautoryzowane.
      if (!server.authenticate(ota_user, ota_pass)) {
        return;
      }

      // Wyłączamy Watchdoga na czas aktualizacji OTA, aby uniknąć restartu
      // podczas długotrwałych operacji zapisu do pamięci flash.
      if (Serial) Serial.println("Rozpoczęto aktualizację OTA. Wyłączam Watchdoga...");
      // WAŻNE: auth sprawdzamy PRZED wdt_delete — odwrócenie tej kolejności spowoduje utratę watchdoga.
      esp_task_wdt_delete(NULL);

      // Używamy rzeczywistego rozmiaru jeśli jest znany, w przeciwnym razie UPDATE_SIZE_UNKNOWN
      size_t fsz = upload.totalSize > 0 ? upload.totalSize : UPDATE_SIZE_UNKNOWN;
      if (!Update.begin(fsz)) { 
        if (Serial) Update.printError(Serial);
        // Błąd został ustawiony w obiekcie Update i zostanie obsłużony w głównym handlerze POST.
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      // Guard na wypadek, gdyby Update.begin() się nie powiodło.
      if (!Update.isRunning() || Update.hasError()) return;
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) { if (Serial) Update.printError(Serial); }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.isRunning()) {
        if (!Update.end(true)) { if (Serial) Update.printError(Serial); }
      }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      if (Serial) Serial.println("OTA przerwane przez klienta. Przywracam Watchdoga...");
      Update.abort(); // Poinformuj bibliotekę o przerwaniu operacji
      esp_task_wdt_add(NULL); // Ponownie zabezpiecz główną pętlę loop()
    }
  });

  server.onNotFound([](){ server.send(404, "text/plain", "404"); });
  server.begin();
}

void setup() {
  pinMode(OUTPUT_PIN, OUTPUT);
  digitalWrite(OUTPUT_PIN, LOW); // Ustaw domyślny, bezpieczny stan na starcie
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, HIGH); // Wyłącz LED na start

  delay(100); // Awaryjny delay na stabilizację napięć i gotowość szyny przed wystartowaniem Seriala

  // Inicjalizacja Serial, ale nie czekaj na połączenie
  Serial.begin(115200);
  // Inteligentne oczekiwanie na port USB CDC (maksymalnie 3 sekundy), nie blokuje sztywno kodu
  unsigned long startSerial = millis();
  while (!Serial && millis() - startSerial < 3000) { delay(10); }

  loadSettings();

  WiFiManager wm;
  wm.setHostname("ZegarAstro");
  wm.setConfigPortalTimeout(120); 

  // --- NOWOŚĆ: Callback dla trybu AP (Miganie 5Hz w tle) ---
  wm.setAPCallback([](WiFiManager *myWiFiManager) {
    apMode = true; // Ustawienie flagi trybu AP
    // Miganie 5Hz (cykl 200ms). Funkcja tick() przełącza stan co 100ms (0.1s).
    ticker.attach(0.1, tick);
  });

  // Uruchomienie managera Wi-Fi
  wm.autoConnect("ZegarAstro_AP"); 

  apMode = false; // Wyjście z trybu AP
  // Po udanym połączeniu (lub timeoucie) zatrzymujemy szybkie miganie Ticker'a (5Hz).
  // Jeśli nastąpił timeout (brak sieci), updateLED() w loop() automatycznie przejdzie w tryb 2Hz.
  ticker.detach();
  digitalWrite(LED_PIN, HIGH); // Zgaszenie, by updateLED mogło przejąć sterowanie

  // Ustawienie strefy czasowej dla Polski (Europa Środkowa, automatycznie przełącza czas letni i zimowy)
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  
  // Rejestracja funkcji fail-safe, która zostanie wywołana przy restarcie/panic
  esp_register_shutdown_handler(shutdown_handler);

  // Konfiguracja sprzętowego Watchdoga (15 sekund timeout)
  // Jeśli pętla loop() zawiesi się na dłużej, urządzenie zostanie automatycznie zrestartowane.
  esp_task_wdt_init(15, true); // Włącz panic on timeout
  esp_task_wdt_add(NULL);      // Dodaj bieżący task (loop) do watchdoga

  setupWebServer();

  updateRelayState();

  // Inicjalizacja timerów długoterminowych po wszystkich operacjach startowych
  unsigned long startup_millis = millis(); // Capture current millis once
  lastCheckTime = startup_millis; // Initialize lastCheckTime
  lastSaveTime = startup_millis; // Initialize lastSaveTime
  lastHeapCheck = startup_millis; // Initialize lastHeapCheck
  lastNTPSync = startup_millis; // Initialize lastNTPSync
  lastServerRestart = startup_millis; // Initialize lastServerRestart
}

void loop() {
  esp_task_wdt_reset(); // "Pogłaszcz" watchdoga na początku każdej pętli
  server.handleClient();
  updateLED(); // Przejmuje obsługę po wyjściu z trybu konfiguracji
  
  unsigned long cm = millis();

  // --- NATYCHMIASTOWA REAKCJA NA SYNCHRONIZACJĘ CZASU ---
  // Wykrywa moment, w którym czas systemowy staje się poprawny (np. po starcie NTP)
  // i natychmiast wymusza przeliczenie przekaźnika i trybu AUTO, bez czekania minuty
  static bool wasTimeValid = false;
  if (!wasTimeValid && time(nullptr) > 1000000000l) {
    wasTimeValid = true;
    updateRelayState();
    if (Serial) Serial.println("Zegar zsynchronizowany. Natychmiastowa aktualizacja stanu trybu AUTO.");
  }

  // --- ZABEZPIECZENIA DLA PRACY DŁUGOTERMINOWEJ (10+ LAT) ---

  // 1. Zoptymalizowany zapis do NVS w celu oszczędzania pamięci flash
  const unsigned long periodicSaveInterval = 21600000UL; // 6 godzin
  bool triggerSave = false;

  // Trigger 1: Zapis okresowy (backup) co 6 godzin
  if (cm - lastSaveTime > periodicSaveInterval) {
    triggerSave = true;
  }

  // Trigger 2: Zmiana ustawień przez użytkownika (np. tryb ON/OFF) lub zmiana stanu przekaźnika
  if (settingsDirty) {
    triggerSave = true;
  }

  // Wykonaj zapis, jeśli którykolwiek z warunków został spełniony
  if (triggerSave) {
    time_t now = time(nullptr);
    if (now > 1000000000l) {
      if (preferences.begin("astro", false)) {
        if (Serial) Serial.println("Wykryto warunek zapisu do NVS...");
        if (settingsDirty) {
          preferences.putInt("mode", currentMode);
          settingsDirty = false; // Wyzeruj flagę po zapisie
        }
        // Zawsze zapisuj czas, gdy następuje jakikolwiek zapis do NVS
        preferences.putLong64("lastTime", (int64_t)now);
        
        uint64_t currentUptime = esp_timer_get_time() / 1000000ULL;
        if (currentUptime > recordUptime) {
          recordUptime = currentUptime;
          preferences.putULong64("recUpt", recordUptime);
        }
        
        preferences.end();
        lastSaveTime = cm; // Zresetuj timer zapisu okresowego
        if (Serial) Serial.println("Zakończono zapis do NVS.");
      } else {
        // Log błędu, ale nie restartuj - spróbujemy za 6h
        if (Serial) Serial.println("Błąd zapisu NVS w pętli loop, pominięto.");
      }
    }
  }

  // Sprawdzenie, czy flaga czasu z NVS może zostać wyczyszczona
  if (g_timeIsFromNVS && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
    g_timeIsFromNVS = false;
    updateRelayState(); // Przelicz przekaźnik ponownie, bo uzyskaliśmy najwyższą precyzję z NTP
    if (Serial) Serial.println("Czas zsynchronizowany z NTP, flaga czasu z NVS wyczyszczona.");
  }

  // 2. Monitorowanie pamięci RAM i prewencyjny restart w razie fragmentacji
  if (cm - lastHeapCheck > 86400000UL) { // Raz na dobę
    lastHeapCheck = cm;
    if (Serial) Serial.printf("HEAP CHECK: Wolna pamięć: %u bajtów\n", ESP.getFreeHeap());
    if (ESP.getFreeHeap() < 10000) { // Jeśli poniżej 10KB
      if (Serial) Serial.println("Krytycznie niski poziom pamięci, restartuję urządzenie...");
      safeRestart(); // Zapisz czas i zrób bezpieczny restart
    }
  }

  // 3. Okresowa synchronizacja czasu NTP, aby przeciwdziałać dryfowi zegara
  if (WiFi.status() == WL_CONNECTED && (cm - lastNTPSync > 604800000UL)) { // Co 7 dni
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    lastNTPSync = cm;
    if (Serial) Serial.println("Wymuszono ponowną synchronizację czasu NTP.");
  }

  // 4. Okresowy restart serwera WWW, aby zapobiec potencjalnym wyciekom pamięci
  if (cm - lastServerRestart > 2592000000UL) { // 30 dni
    if (Serial) Serial.println("Prewencyjny restart serwera WWW...");
    esp_task_wdt_reset(); // Nakarm watchdoga przed operacją
    server.stop();
    // Zamiast delay() — użyj nieblokującego resetu
    unsigned long stopTime = millis();
    while (millis() - stopTime < 100) {
        esp_task_wdt_reset();
        yield(); // Pozwól na obsługę zdarzeń systemowych
    }
    server.begin();
    lastServerRestart = cm;
    if (Serial) Serial.println("Serwer WWW zrestartowany.");
  }

  // --- KONIEC ZABEZPIECZEŃ DŁUGOTERMINOWYCH ---


  // Przycisk Config (5s) z debouncingiem
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (buttonDownTime == 0) { // Pierwsze wykrycie wciśnięcia
      buttonDownTime = cm;
    }
    // Po 50ms uznajemy, że przycisk jest stabilnie wciśnięty (debounce)
    if (cm - buttonDownTime > 50) {
      if (!isButtonPressed) { // Rejestrujemy wciśnięcie po raz pierwszy
        isButtonPressed = true;
        buttonPressTime = cm; // Rozpoczynamy odliczanie 5 sekund
      } else if (cm - buttonPressTime > 5000) {
        WiFiManager wm; wm.resetSettings(); safeRestart();
      }
    }
  } else {
    buttonDownTime = 0; // Reset timera debounce
    isButtonPressed = false; // Reset stanu przycisku
  }
  // Uparty Watchdog Wi-Fi
  static int reconnectAttempts = 0; // Licznik prób ponownego połączenia z WiFi
  static unsigned long lastRadioResetTime = 0; // Osobny timer dla 10-minutowych restartów radia
  if (cm - lastWiFiCheck >= wifiCheckInterval) {
    if (WiFi.status() != WL_CONNECTED) {
      if (!isWifiDisconnected) {
        wifiDisconnectStartTime = cm;
        lastRadioResetTime = cm;
        isWifiDisconnected = true;
        reconnectAttempts = 0; // Zresetuj licznik przy pierwszym wykryciu rozłączenia
      }
      // Próbuj połączyć ponownie tylko przy twardym rozłączeniu, ale z ograniczeniem prób, aby uniknąć pętli
      if (WiFi.status() == WL_DISCONNECTED) {
        if (reconnectAttempts < 10) {
          if (Serial) Serial.printf("Próba ponownego połączenia z WiFi, próba #%d...\n", reconnectAttempts + 1);
          WiFi.reconnect();
          reconnectAttempts++;
        } else {
          // Po 10 nieudanych próbach przestajemy aktywnie próbować co 30s.
          // Pozwalamy zadziałać dalszym, rzadszym mechanizmom watchdoga (reset radia po 10 min).
          if (reconnectAttempts == 10) { // Loguj tylko raz
            if (Serial) Serial.println("Osiągnięto limit szybkich prób ponownego połączenia. Przechodzę w tryb pasywny.");
            reconnectAttempts++; // Zwiększ, aby nie logować ponownie
          }
        }
      }

      // 4. Po 24h braku połączenia, uruchom portal ratunkowy na 5 minut
      if (isWifiDisconnected && (cm - wifiDisconnectStartTime > 86400000UL)) {
        if (Serial) Serial.println("Brak połączenia WiFi przez 24h. Uruchamiam portal ratunkowy na 5 minut...");
        
        esp_task_wdt_delete(NULL); // WYŁĄCZ WDT na czas blokującego interfejsu (inaczej nastąpi 100% pewny restart w 15 sekundzie)
        
        WiFiManager wm;
        wm.setConfigPortalTimeout(300); // 5 minut
        wm.setAPCallback([](WiFiManager *myWiFiManager) { apMode = true; ticker.attach(0.1, tick); });
        wm.startConfigPortal("ZegarAstro_Rescue");
        apMode = false;
        ticker.detach();
        digitalWrite(LED_PIN, HIGH);
        // Zresetuj timer, aby kolejna próba była za 24h
        wifiDisconnectStartTime = millis();
        lastRadioResetTime = millis();
        
        esp_task_wdt_add(NULL); // PONOWNIE WŁĄCZ WDT po zakończeniu pracy portalu ratunkowego
      }
      // Po 10 minutach braku połączenia (ale przed 24h), zresetuj radio
      else if (isWifiDisconnected && (cm - lastRadioResetTime > 600000)) { 
        WiFi.disconnect(false); WiFi.mode(WIFI_OFF); delay(100); WiFi.mode(WIFI_STA); WiFi.begin();
        lastRadioResetTime = cm; // Resetuj TYLKO timer restartu radia na kolejne 10 minut
        reconnectAttempts = 0; // Zresetuj licznik po resecie radia
      }
    } else { 
      if (isWifiDisconnected) { // Jeśli to pierwsze sprawdzenie po odzyskaniu połączenia
        if (Serial) Serial.println("Połączenie WiFi zostało przywrócone.");
        reconnectAttempts = 0; // Zresetuj licznik po udanym połączeniu
      }
      isWifiDisconnected = false; /* Połączenie wróciło */ 
    }
    lastWiFiCheck = cm;
  }

  if (cm - lastCheckTime > checkInterval) { updateRelayState(); lastCheckTime = cm; }
}