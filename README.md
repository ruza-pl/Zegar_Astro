# 🌞 Zegar Astronomiczny IoT (ESP32-C3)

Inteligentny zegar astronomiczny oparty na mikrokontrolerze **ESP32-C3**, sterujący przekaźnikiem na podstawie dokładnych godzin wschodu i zachodu słońca. Urządzenie zostało zaprojektowane z myślą o wieloletniej, bezobsługowej pracy (np. do sterowania oświetleniem zewnętrznym lub integracji z centralami alarmowymi typu Satel Integra).

## ✨ Główne funkcje

* 📍 **Wbudowana mapa i geolokalizacja:** Interfejs WWW zawiera interaktywną mapę (Leaflet/OpenStreetMap) ułatwiającą szybkie ustawienie współrzędnych.
* ☀️ **Precyzyjne obliczenia astronomiczne:** Obliczanie wschodu i zachodu słońca całkowicie offline na podstawie podanej lokalizacji.
* 🎛️ **Zaawansowane sterowanie czasem:** Możliwość ustawienia offsetu (przesunięcia w minutach) niezależnie dla wschodu i zachodu słońca.
* 📱 **Nowoczesny Web UI (Dark Mode):** Responsywny, ciemny interfejs użytkownika zoptymalizowany pod urządzenia mobilne i komputery.
* 🔄 **Aktualizacje OTA (Over-The-Air):** Bezpieczne wgrywanie nowego oprogramowania (`.bin`) bezpośrednio przez przeglądarkę internetową, z pełną obsługą błędów i przerwanych połączeń.
* ⏱️ **Kuloodporny Czas (NTP + NVS):** Synchronizacja czasu przez NTP. Jako zabezpieczenie przed brakiem internetu po restarcie, urządzenie zapisuje czas w pamięci NVS. Projekt w 100% używa typu 64-bitowego (odporność na błąd roku 2038 - Y2038).

## 🛡️ Niezawodność i Systemy Failsafe
Zegar został zaprojektowany z myślą o bezawaryjnej pracy ciągłej przez dekady. Wyposażono go w zaawansowane mechanizmy chroniące przed typowymi dla systemów wbudowanych problemami:
* **Ochrona NVS (Anty-Bootloop):** W przypadku uszkodzenia struktury pamięci flash, zegar próbuje ją awaryjnie sformatować. Jeśli uszkodzenie jest fizyczne/trwałe, urządzenie nie wpada w nieskończoną pętlę restartów. Automatycznie ładuje bezpieczne wartości domyślne do pamięci RAM i podejmuje normalną pracę do czasu wyłączenia zasilania.
* **Sprzętowy Watchdog (WDT):** Monitoruje pętlę główną. W przypadku zawieszenia programu powyżej 15 sekund wymusza twardy reset mikrokontrolera. Jest dynamicznie pauzowany podczas procesów blokujących (np. wgrywanie OTA, Portal Wi-Fi), aby zapobiec błędnym restartom.
* **Shutdown Handler (Bezpieczny stan wyjścia):** Wykorzystanie natywnego API systemu ESP-IDF do zarejestrowania procedury ratunkowej. W razie krytycznego błędu (Panic) lub sprzętowego restartu, procesor przed odcięciem zasilania natychmiast przełącza przekaźnik w bezpieczny stan niski (LOW).
* **Ochrona pamięci (OOM Failsafe):** System monitoruje wolną pamięć RAM co 24 godziny. Przy skrajnej fragmentacji (poniżej 10 KB wolnego) wykonuje prewencyjny, bezpieczny restart (z zachowaniem stanów). Dodatkowo, co 30 dni dyskretnie restartowany jest sam proces serwera WWW.
* **Niezawodność sieci Wi-Fi:** Zegar używa inteligentnego algorytmu ponownych połączeń (max 10 szybkich prób, by nie zablokować domowego routera spamem). Po 10 minutach braku połączenia resetuje sprzętowo własne radio Wi-Fi. Po 24 godzinach automatycznie uruchamia ratunkowy Captive Portal dla użytkownika.
* **Podtrzymanie działania bez internetu:** Czas operacyjny jest cyklicznie backupowany do pamięci NVS. Jeśli po zaniku zasilania domowe Wi-Fi lub serwery NTP będą niedostępne, urządzenie odtworzy ostatni znany czas (dodając offset na start) i będzie bez przerw kontynuować harmonogram astronomiczny.
* **Zabezpieczenie transferu OTA:** Zerwane połączenie z przeglądarką w trakcie wgrywania oprogramowania (Upload Abort) jest poprawnie przechwytywane. Urządzenie usuwa "resztki" pliku, zamyka zapis, przywraca zabezpieczenie Watchdoga i wraca do normalnej pracy bez zawieszania systemu.

## ⚙️ Tryby Pracy

1. **AUTO (Astro)** - Przekaźnik załącza się automatycznie o zachodzie słońca i wyłącza o wschodzie.
2. **Wymuś ON** - Przekaźnik włączony na stałe.
3. **Wymuś OFF** - Przekaźnik wyłączony na stałe.
4. **Inwersja wyjścia** - Możliwość odwrócenia logiki pracy (załączanie w dzień, wyłączanie w nocy).

## 🔧 Wymagania sprzętowe

* Płytka: **ESP32-C3 Supermini** (lub kompatybilna ESP32 / ESP32-C3)
* Zasilanie: 3.3V lub 5V
* Wyjście: np. transoptor **EL817** (do sterowania wejściami centrali alarmowej) lub przekaźnik.
* Elementy dodatkowe: Opcjonalny przycisk sprzętowy (BOOT) do wybudzania portalu konfiguracyjnego.

### Konfiguracja PIN-ów (domyślna)
* `OUTPUT_PIN` = 5
* `LED_PIN` = 8 (Wbudowana dioda LED)
* `BUTTON_PIN` = 9 (Przycisk BOOT na płytce Supermini)

## 🚀 Instalacja i Pierwsze Uruchomienie

1. Skompiluj i wgraj oprogramowanie za pomocą **PlatformIO** w środowisku VSCode. Projekt wykorzystuje specjalny skrypt `increment_version.py` do automatycznego podnoszenia numeru *build* przy każdej kompilacji.
2. Przy pierwszym uruchomieniu (lub po wyczyszczeniu flasha) urządzenie utworzy punkt dostępowy Wi-Fi (AP) o nazwie **ZegarAstro_AP** z portalem logowania.
3. Połącz się z tą siecią, wejdź pod adres `http://192.168.4.1` i wprowadź dane swojej domowej sieci Wi-Fi.
4. Zegar zrestartuje się, podłączy do sieci, pobierze czas, a wbudowana dioda LED wskaże stan działania urządzenia.
5. Wpisz adres IP zegara w swojej domowej przeglądarce, aby uzyskać dostęp do mapy i ustawień astro.

## 🔐 Aktualizacje OTA (Over-The-Air)

Urządzenie obsługuje wygodne i bezprzewodowe aktualizacje oprogramowania (OTA) z poziomu przeglądarki internetowej. Aby zapobiec nieautoryzowanym zmianom oprogramowania, dostęp do panelu aktualizacji jest zabezpieczony uwierzytelnianiem.

**Domyślne dane logowania:**
* **Użytkownik:** `admin`
* **Hasło:** `astro123`

> **⚠️ Ważne:** Ze względów bezpieczeństwa, przed wdrożeniem urządzenia zaleca się bezwzględną zmianę domyślnego hasła. Możesz to zrobić, edytując zmienne `ota_user` i `ota_pass` w pliku `src/main.cpp`.

### Jak wykonać aktualizację?
1. Skompiluj projekt (np. w PlatformIO), aby uzyskać plik `.bin` z nowym oprogramowaniem.
2. Otwórz przeglądarkę internetową i przejdź pod adres: `http://<adres_IP_urządzenia>/update`.
3. W oknie logowania podaj powyższą nazwę użytkownika oraz hasło.
4. Wybierz skompilowany plik `.bin` ze swojego dysku, a następnie kliknij przycisk aktualizacji. Po udanym procesie urządzenie zrestartuje się automatycznie.

## � Wskaźnik LED (Diagnostyka)
Wbudowana dioda LED informuje o statusie systemu:
* **Szybkie miganie (5Hz):** Tryb AP (Portal konfiguracyjny) - Oczekuje na konfigurację Wi-Fi.
* **Miganie (2Hz):** Brak połączenia z Wi-Fi.
* **Miganie powolne (1Hz):** Połączono z Wi-Fi, brak synchronizacji czasu z NTP.
* **Bardzo rzadkie błyśnięcia (0.5Hz):** Zegar połączony i zsynchronizowany, przekaźnik wyłączony (Dzień).
* **Świeci ciągłym światłem:** Zegar połączony i zsynchronizowany, przekaźnik włączony (Noc).

## 🌐 Endpointy API (HTTP GET)
Zegar udostępnia proste API umożliwiające szybką integrację z systemami Smart Home (np. Home Assistant):
* `/api/on` - Wymusza stan wysoki
* `/api/off` - Wymusza stan niski
* `/api/auto` - Wraca do trybu automatycznego (Astro)
* `/api/status` - Zwraca status urządzenia w formacie JSON (pamięć, uptime, status wifi/ntp, obecny tryb)

## 🛠️ Zastosowane Technologie
* **Framework:** Arduino core for ESP32
* **Środowisko:** PlatformIO
* **Biblioteki:** 
  * `WiFiManager` (v2.0.17) - obsługa portalu captive
  * `Dusk2Dawn` - obliczenia matematyczne Słońca
  * `Preferences` - szybki zapis/odczyt NVS
  * `WebServer` i `Update` - interfejs i OTA

---
*Wersja 1.0 | © 2026 Piotr Rużański*
