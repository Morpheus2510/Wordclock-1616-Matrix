Dieses Projekt ist eine Wort-Uhr (WordClock) mit einer 16×16-LED-Matrix auf Basis eines ESP32 und einer DS3231-RTC. Über ein Web-Interface können WLAN-Einstellungen, Firmware-Updates, Farbeinstellungen und mehr konfiguriert werden.

Features
16×16-LED-Matrix (NeoPixel / WS2812b)
DS3231 RTC für genaue Zeitmessung (auch offline).
WLAN-Konfiguration (entweder DHCP oder statische IP).
Web-Interface zum Einstellen von:
Farbe (RGB-Farbwahl)
Helligkeit
Regenbogenmodus (falls gewünscht)
Zeit-Offset (Zeitzonen-Anpassung)
Tagespläne (Dimmen oder Ausschalten nach Zeitplan)
Firmware-Update (OTA) via HTTP.
Access Point-Modus, falls kein WLAN bekannt ist.
OTA-Update (Over-the-Air) über das Web-Interface auf /update.
Aufbau & Verkabelung
ESP32-Modul, z. B. ein ESP32-DevKitC.
DS3231-RTC-Modul an I2C (SDA/SCL).
LED-Streifen oder 16×16-Matrix am Datenpin (in diesem Code Pin 32).
Achte darauf, dass die 5V/3.3V-Versorgung je nach LED-Anzahl ausreichend dimensioniert ist. Die DS3231 wird in der Regel mit 3.3V oder 5V versorgt (bitte Datenblatt beachten).

Erforderliche Bibliotheken
NeoPixelBrightnessBus (für die LED-Ansteuerung)
RTClib (Adafruit) für die DS3231
Preferences (ESP32-Standard)
ArduinoOTA (ESP32-Standard)
HTTPClient & HTTPUpdate (für Online-Updates)
In der Arduino-IDE kannst du diese über den Bibliotheksverwalter installieren.

Installation
Code herunterladen:
Lade das Projekt als ZIP herunter oder klone das Repo.
Öffne die .ino-Datei in der Arduino-IDE oder PlatformIO.
Bibliotheken installieren:
Stelle sicher, dass du oben genannte Bibliotheken installiert hast.
Anpassen (optional):
Falls du einen anderen Daten-Pin (für LEDs) nutzt, passe DATA_PIN an.
Wenn du eine andere LED-Anzahl nutzt, ändere NUM_LEDS.
Für eine andere Matrix-Größe die MATRIX_WIDTH / MATRIX_HEIGHT anpassen.
Upload:
Schließe deinen ESP32 an und wähle den richtigen Port in der Arduino-IDE.
Drücke Upload (Strg + U).
Beim ersten Start erstellt der Sketch ein WLAN-Access-Point namens WordClockAP mit Passwort password123. Du kannst dich damit verbinden und anschließend im Browser http://192.168.4.1 aufrufen, um die Uhr zu konfigurieren.

Web-Interface
http://<IP-Adresse>/ – Hauptseite, zeigt Konfigurationslinks.
/wifi – WLAN-Konfiguration (SSID/Passwort).
/configure_main – Farbe, Helligkeit, Regenbogen.
/diagnose – Diagnose-Infos (z. B. Uhrzeit, WLAN-Status, RTC-Temp).
/update – OTA-Firmware-Update.
Zeigt die aktuell hinterlegte Firmware-URL an (Standard ist dein GitHub-Link).
Klick auf „Jetzt Update einspielen“ lädt eine neue Firmware vom Link hoch.
Firmware-Update (OTA)
Gehe auf http://<IP>/update.
Die Seite zeigt eine Formularzeile Firmware-URL (Standard:
bash
Code kopieren
https://github.com/Morpheus2510/Wordclock-1616-Matrix/releases/download/v1_9_9/WordClock_v1_9_9.ino.bin
).
URL speichern – Du kannst die URL ändern und dann speichern, um eine andere Version einzuspielen.
„Jetzt Update einspielen“ – startet das OTA-Update vom Server.
Nach erfolgreichem Update startet der ESP32 automatisch neu.

Zeiteinstellung
Das System versucht zuerst NTP (wenn WLAN vorhanden).
Falls kein WLAN oder kein NTP, wird die DS3231 ausgelesen.
Wenn die DS3231 einen Stromausfall hatte, wird sie aktualisiert, sobald wieder NTP verfügbar ist.
Tagespläne
Unter http://<IP>/configure_day_schedules kannst du pro Wochentag eine Start- und Endzeit definieren sowie wählen, ob in dieser Zeit gedimmt oder ausgeschaltet werden soll.

Hinweise
Wenn du bereits einmal eine andere Firmware-URL gespeichert hast, überschreibt die Preferences deinen Standard-Link. Dann musst du entweder die URL manuell ändern oder die Preferences leeren (per /reset).
Die DS3231-RTC solltest du sicher mit dem ESP32-GND verbinden, und SCL/SDA (Standard: GPIO 22 / GPIO 21) korrekt anschließen.
