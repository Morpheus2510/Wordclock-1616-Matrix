# Wordclock-1616-Matrix
Wordclock  for 16x16 led matrix esp32
Word Clock mit ESP32 und Neopixel Matrix
Einleitung
Diese Anleitung beschreibt die Einrichtung und Verwendung einer Wortuhr basierend auf einem ESP32 Mikrocontroller und einer Neopixel LED-Matrix. Die Uhr zeigt die aktuelle Uhrzeit in Worten an und bietet verschiedene Konfigurationsmöglichkeiten über ein Webinterface.

Features
Anzeigen der Uhrzeit in Worten auf einer 16x16 LED-Matrix.
Webbasiertes Konfigurationsportal für einfache Einstellungen.
Online- und Offline-Modus: Verwendung von NTP für genaue Zeit oder manuelle Zeiteinstellung.
Farb- und Helligkeitsanpassung der Anzeige.
Regenbogenmodus für eine dynamische Farbdarstellung.
Tagespläne zur Steuerung der Helligkeit und Betriebszeiten an Wochentagen.
Speicherung von Einstellungen im nicht-flüchtigen Speicher (Preferences).
Hardwareanforderungen
ESP32 Entwicklungsboard
Neopixel (WS2812B) 16x16 LED-Matrix (insgesamt 256 LEDs)
5V Netzteil mit ausreichender Stromstärke (mindestens 10A empfohlen)
Verkabelung zur Verbindung des ESP32 mit der LED-Matrix:
Datenleitung von Pin 32 des ESP32 zum Data-In der LED-Matrix
Gemeinsame Masseverbindung zwischen ESP32 und LED-Matrix
Softwareanforderungen
Arduino IDE (empfohlen Version 1.8.13 oder höher)
ESP32 Boardunterstützung in der Arduino IDE installiert
Benötigte Bibliotheken:

WiFi.h
WebServer.h
WiFiUdp.h
NeoPixelBrightnessBus.h
time.h
Preferences.h
Aufbauanleitung
Verkabelung herstellen:

Verbinde den Datenpin DATA_PIN (standardmäßig Pin 32) des ESP32 mit dem Data-In der LED-Matrix.
Stelle sicher, dass die Masse (GND) des ESP32 mit der Masse der LED-Matrix und dem Netzteil verbunden ist.
Schließe die 5V Spannungsversorgung an die LED-Matrix an.
Achtung: Verbinde die 5V nicht direkt mit dem ESP32, da dieser mit 3,3V arbeitet.
Stromversorgung:

Schließe ein ausreichend dimensioniertes Netzteil an, um die LED-Matrix mit Strom zu versorgen.
Der ESP32 kann separat über USB oder ebenfalls über das Netzteil (unter Beachtung der Spannung) versorgt werden.
Installationsanleitung
Arduino IDE vorbereiten:

Stelle sicher, dass die ESP32 Boardunterstützung installiert ist.
Installiere die benötigten Bibliotheken über den Bibliotheksmanager oder manuell.
Code in die Arduino IDE laden:

Kopiere den bereitgestellten Quellcode in ein neues Sketch in der Arduino IDE.
Anpassungen im Code (falls erforderlich):

Überprüfe die Pin-Definition für DATA_PIN und passe sie gegebenenfalls an.
Passe die SSID und das Passwort für den Access Point in der Funktion startAccessPoint() an:
cpp
Code kopieren
WiFi.softAP("WordClockAP", "dein_sicheres_passwort", 1, false, 10);
Hinweis: Das Passwort muss mindestens 8 Zeichen lang sein.
Code kompilieren und auf den ESP32 hochladen:

Wähle das richtige ESP32 Board in der Arduino IDE aus.
Kompiliere das Sketch und lade es auf den ESP32 hoch.
Bedienungsanleitung
Erstinbetriebnahme
Access Point verbinden:

Nach dem Start erstellt der ESP32 einen Wi-Fi Access Point mit dem Namen WordClockAP.
Verbinde dich mit diesem Netzwerk. Das standardmäßige Passwort ist dein_sicheres_passwort (wie im Code festgelegt).
Webinterface aufrufen:

Öffne einen Webbrowser und navigiere zur IP-Adresse des Access Points, in der Regel 192.168.4.1.
WLAN konfigurieren:

Navigiere im Webinterface zu den Wi-Fi Einstellungen.
Wähle dein Heimnetzwerk aus der Liste der verfügbaren Netzwerke aus und gib das Passwort ein.
Speichere die Einstellungen. Der ESP32 versucht nun, sich mit deinem Heimnetzwerk zu verbinden.
Webinterface im Heimnetzwerk:

Nachdem der ESP32 mit deinem WLAN verbunden ist, erhält er eine IP-Adresse in deinem Heimnetzwerk.
Finde die IP-Adresse heraus (wird im seriellen Monitor angezeigt oder im Router) und rufe das Webinterface erneut auf.
Funktionen des Webinterfaces
Startseite:

Zeigt die aktuelle Uhrzeit, den Verbindungsstatus und die IP-Adresse an.
Bietet Links zu den verschiedenen Konfigurationsseiten.
Wi-Fi Einstellungen:

Ermöglicht das Verbinden mit einem WLAN-Netzwerk.
Zeigt den Verbindungsstatus an.
Manuelle Zeit einstellen:

Ermöglicht das Setzen der Uhrzeit im Offline-Modus.
Nützlich, wenn keine Internetverbindung für NTP vorhanden ist.
Modus wechseln:

Umschalten zwischen Online-Modus (NTP-Zeit) und Offline-Modus (manuelle Zeit).
Zeit-Offset einstellen:

Einstellen eines Zeitversatzes in Minuten (z.B. für Sommerzeitkorrekturen).
Tagespläne einstellen:

Konfiguration von Ein- und Ausschaltzeiten für jeden Wochentag.
Option zum Dimmen statt vollständigem Ausschalten.
Hauptkonfiguration:

Anpassung der Farbe und Helligkeit der LEDs.
Aktivierung des Regenbogenmodus für eine dynamische Farbdarstellung.
Anpassungsmöglichkeiten
Farbe und Helligkeit
Farbe: Wähle eine beliebige Farbe über den Farbwähler im Webinterface.
Helligkeit: Stelle die Helligkeit der LEDs zwischen 0 und 255 ein.
Regenbogenmodus
Aktiviert einen Modus, in dem die Farben der Worte in Regenbogenfarben angezeigt werden.
Jede Reihe der LED-Matrix hat eine eigene Farbe, die sich kontinuierlich ändert.
Tagespläne
Start- und Endzeiten: Lege fest, wann die Uhr aktiv sein soll.
Dimmed-Modus: Wähle, ob die Uhr in der inaktiven Zeit gedimmt oder komplett ausgeschaltet sein soll.
Troubleshooting
Der Access Point erscheint als ungesichert:

Stelle sicher, dass das Passwort für den Access Point mindestens 8 Zeichen lang ist.
Überprüfe die Funktion startAccessPoint() und passe das Passwort an.
Keine Verbindung zum WLAN:

Überprüfe die eingegebene SSID und das Passwort im Webinterface.
Stelle sicher, dass der ESP32 in Reichweite des Routers ist.
Zeit wird nicht synchronisiert:

Stelle sicher, dass der ESP32 mit dem Internet verbunden ist.
Überprüfe die NTP-Konfiguration und die Zeitzoneneinstellung.
LEDs bleiben aus:
Überprüfe die Stromversorgung der LED-Matrix.
Stelle sicher, dass die Helligkeit nicht auf 0 gesetzt ist.
Überprüfe die Tagespläne, ob die Uhr in der aktuellen Zeit aktiv ist.
