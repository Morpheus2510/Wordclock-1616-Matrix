# WordClock 16×16

Dieses Projekt ist eine **Wort-Uhr** (WordClock) mit einer 16×16-LED-Matrix auf Basis eines ESP32 und einer DS3231-RTC. Über ein Web-Interface können WLAN-Einstellungen, Firmware-Updates, Farbeinstellungen und mehr konfiguriert werden.

---

## Features

1. **16×16-LED-Matrix** (NeoPixel / WS2812b)  
2. **DS3231 RTC** für genaue Zeitmessung (auch offline)  
3. **WLAN-Konfiguration** (DHCP oder statische IP)  
4. **Web-Interface** zum Einstellen von  
   - Farbe (RGB-Farbwahl)  
   - Helligkeit  
   - Regenbogenmodus (falls gewünscht)  
   - Zeit-Offset (Zeitzonen-Anpassung)  
   - Tagespläne (Dimmen / Ausschalten)  
   - Firmware-Update (OTA) via HTTP  
5. **Access Point**-Modus, falls kein WLAN bekannt ist  
6. **OTA-Update** (Over-the-Air) über das Web-Interface auf `http://<IP>/update`.

---

## Aufbau & Verkabelung

- **ESP32**-Modul (z. B. ein ESP32-DevKitC)  
- **DS3231 RTC**-Modul an I2C (SDA/SCL)  
- **LED-Streifen** oder 16×16-**Matrix** am Datenpin (hier Pin `32`)  

Achte darauf, dass die 5 V / 3,3 V-Versorgung für deine LED-Anzahl ausreicht. Die DS3231-RTC benötigt i. d. R. 3,3 V oder 5 V (siehe Datenblatt).

---

## Erforderliche Bibliotheken

- **NeoPixelBrightnessBus** (für die LED-Ansteuerung)  
- **RTClib** (von Adafruit)  
- **Preferences** (ESP32-Standard)  
- **ArduinoOTA** (ESP32-Standard)  
- **HTTPClient** & **HTTPUpdate** (für Online-Updates)

*(Über den Bibliotheksverwalter der Arduino-IDE installierbar.)*

---

## Installation

1. **Code herunterladen**  
   - Lade das Projekt als ZIP herunter oder klone das Repo.  
   - Öffne die `.ino`-Datei in der Arduino-IDE oder PlatformIO.
2. **Bibliotheken installieren**  
   - Stelle sicher, dass die oben genannten Bibliotheken installiert sind.  
3. **Einstellungen anpassen** (optional)  
   - Pin für die LEDs (`DATA_PIN`) ändern, falls du einen anderen Pin nutzt.  
   - `NUM_LEDS`, falls du eine andere Anzahl an LEDs hast.  
4. **Upload**  
   - Verbinde den ESP32 per USB mit deinem Rechner.  
   - Wähle den richtigen Port in der Arduino-IDE.  
   - Klicke auf **Upload**.

Beim ersten Start erstellt der Sketch ein WLAN-Access-Point namens **`WordClockAP`** mit Passwort **`password123`**. Du kannst dich damit verbinden und dann im Browser `http://192.168.4.1` aufrufen, um die Uhr zu konfigurieren.

---

## Web-Interface

- `http://<IP>/`  
  – Hauptseite, zeigt Konfigurationslinks.  
- `http://<IP>/wifi`  
  – WLAN-Konfiguration (SSID / Passwort).  
- `http://<IP>/configure_main`  
  – Farbe, Helligkeit, Regenbogen.  
- `http://<IP>/diagnose`  
  – Diagnose-Infos (Uhrzeit, WLAN-Status, RTC-Temperatur).  
- `http://<IP>/update`  
  – OTA-Firmware-Update. Zeigt die aktuell hinterlegte **Firmware-URL** an (Standard ist dein GitHub-Link). Bei Klick auf „Jetzt Update einspielen“ wird ein neues Binärimage geladen.

---

## Firmware-Update (OTA)

1. Öffne `http://<IP>/update`.  
2. Du siehst die **aktuelle Firmware-URL** (z. B.  
   ```none
   https://github.com/Morpheus2510/Wordclock-1616-Matrix/releases/download/v1_9_9/WordClock_v1_9_9.ino.bin
3. URL speichern – Du kannst die URL ändern und dann speichern, um eine andere Version einzuspielen.
4. „Jetzt Update einspielen“ – startet das OTA-Update vom Server.

Nach erfolgreichem Update startet der ESP32 automatisch neu.

---

## Zeiteinstellung
Das System versucht zuerst NTP (wenn WLAN vorhanden).
Falls kein WLAN oder kein NTP verfügbar, wird die DS3231 genutzt.
Wenn die DS3231 einen Stromausfall hatte, wird sie aktualisiert, sobald wieder NTP verfügbar ist.

---

## Tagespläne
Unter http://<IP>/configure_day_schedules kannst du pro Wochentag eine Start- und Endzeit definieren und wählen, ob die LEDs in diesem Zeitraum gedimmt oder ausgeschaltet werden sollen.

---

## Hinweise
Hast du bereits einmal eine andere Firmware-URL gespeichert, überschreibt die Preferences deinen Standard-Link.
Dann musst du die URL manuell ändern oder die Preferences per /reset leeren.
Verbinde die DS3231-RTC sicher mit ESP32-GND und schließe SCL/SDA an (Standard: GPIO 22 / 21).
