#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <NeoPixelBrightnessBus.h>
#include <time.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>

// LED-Konfiguration
#define NUM_LEDS 256  // Gesamtzahl der LEDs in der Matrix
#define DATA_PIN 32

// Matrix-Größe
#define MATRIX_WIDTH 16
#define MATRIX_HEIGHT 16

// Anzahl der aktiven Reihen mit LEDs
#define NUM_ACTIVE_ROWS 8

// NeoPixelBrightnessBus Objekt erstellen
NeoPixelBrightnessBus<NeoGrbFeature, NeoEsp32Rmt0800KbpsMethod> strip(NUM_LEDS, DATA_PIN);

// Webserver initialisieren
WebServer server(80);

// Globale Variablen
uint8_t brightness = 50; // Standardhelligkeit (0-255)
RgbColor selectedColor(255, 255, 255); // Standardfarbe Weiß
bool rainbowMode = false; // Regenbogenmodus deaktiviert

// Diese URL zeigt auf Ihre neue Firmware
String firmwareURL = "http://example.com/firmware.bin";

// Offline-Modus Variablen
bool useManualTime = false; // Standardmäßig Online-Modus
struct tm manualTime; // Manuell gesetzte Zeit
int timeOffsetMinutes = 0; // Fester Zeit-Offset in Minuten

// Preferences zum Speichern von Einstellungen
Preferences preferences;

// LED-Wort-Indizes Struktur
struct WordIndices {
  int start;
  int end;
};

// Stundenwörter (angepasste Reihenfolge)
WordIndices STUNDENADR[12] = {
  {91, 89},    // Ein       (Index 0) für 1:00 Uhr
  {127, 124},  // Zwei      (Index 1)
  {123, 120},  // Drei      (Index 2)
  {115, 112},  // Vier      (Index 3)
  {83, 80},    // Fünf      (Index 4)
  {88, 84},    // Sechs     (Index 5)
  {63, 58},    // Sieben    (Index 6)
  {95, 92},    // Acht      (Index 7)
  {119, 116},  // Neun      (Index 8)
  {150, 147},  // Zehn      (Index 9)
  {146, 144},  // Elf       (Index 10)
  {57, 53}     // Zwölf     (Index 11)
};

// Zusätzliche Wörter
WordIndices ES = {255, 254};
WordIndices IST = {252, 250};
WordIndices ToPastAddr[2] = {
  {159, 157},  // Vor
  {179, 176}   // Nach
};
WordIndices HalfFullHourAddr[2] = {
  {155, 152},  // Halb
  {50, 48}     // Uhr
};

// LED-Wort für "Minute" und "Minuten"
WordIndices MINUTE = {23, 18};    // "Minute" Wort
WordIndices MINUTEN = {23, 17};  // "Minuten" Wort (angepasst)

// Minutenwörter (z.B., "Fünf", "Zehn", "Viertel", "Zwanzig")
WordIndices MINUTENADR[4] = {
  {191, 188},  // Fünf
  {211, 208},  // Zehn
  {187, 181},  // Viertel
  {223, 217}   // Zwanzig
};

// Hinzugefügte LEDs für die Minutenanzeige
const int LED_PLUS = 1; // Beispielindex für '+'
const int LED_1 = 3;    // Beispielindex für '1'
const int LED_2 = 4;    // Beispielindex für '2'
const int LED_3 = 5;    // Beispielindex für '3'
const int LED_4 = 6;    // Beispielindex für '4'

// LED-Zuordnung pro aktiver Reihe
const int rowLeds[NUM_ACTIVE_ROWS][16] = {
  {255, 254, 253, 252, 251, 250, 249, 248, 247, 246, 245, 244, 243, 242, 241, 240}, // Reihe 0
  {223, 222, 221, 220, 219, 218, 217, 216, 215, 214, 213, 212, 211, 210, 209, 208}, // Reihe 1
  {191, 190, 189, 188, 187, 186, 185, 184, 183, 182, 181, 180, 179, 178, 177, 176}, // Reihe 2
  {159, 158, 157, 156, 155, 154, 153, 152, 151, 150, 149, 148, 147, 146, 145, 144}, // Reihe 3
  {127, 126, 125, 124, 123, 122, 121, 120, 119, 118, 117, 116, 115, 114, 113, 112}, // Reihe 4
  {95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80},                     // Reihe 5
  {63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48},                     // Reihe 6
  {31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16}                      // Reihe 7
};


// Zusätzliche WordIndices für "Eins"
WordIndices EINS = {91, 88}; // "Eins" Wort

// Struktur zur Speicherung der Tagespläne
struct DaySchedule {
  int startHour;    // Startstunde (0-23)
  int startMinute;  // Startminute (0-59)
  int endHour;      // Endstunde (0-23)
  int endMinute;    // Endminute (0-59)
  bool dimmed;      // Dimmed-Modus aktivieren
};

// Array für alle Wochentage (0 = Sonntag, 1 = Montag, ..., 6 = Samstag)
DaySchedule weekSchedules[7];

// Array zur Speicherung des aktuellen Zustands jeder LED
RgbColor ledState[NUM_LEDS];

// Setzt ein Wort basierend auf den WordIndices unter Berücksichtigung des LED-Zustands und Priorität
void setWordWithPriority(WordIndices word, RgbColor color, bool highPriority = false) {
  Serial.printf("Setze Wort mit Priorität von LED %d bis %d mit Farbe RGB(%d, %d, %d)\n", word.start, word.end, color.R, color.G, color.B);
  if (word.start <= word.end) {
    for (int i = word.start; i <= word.end; i++) {
      if (i >= 0 && i < NUM_LEDS) {
        if (highPriority || (ledState[i].R == 0 && ledState[i].G == 0 && ledState[i].B == 0)) {
          ledState[i] = color; // Aktualisiere den LED-Zustand nur bei hoher Priorität oder wenn die LED ausgeschaltet ist
          strip.SetPixelColor(i, ledState[i]);
        }
      } else {
        Serial.printf("WARNUNG: LED-Index %d außerhalb des Bereichs!\n", i);
      }
    }
  } else {
    for (int i = word.start; i >= word.end; i--) {
      if (i >= 0 && i < NUM_LEDS) {
        if (highPriority || (ledState[i].R == 0 && ledState[i].G == 0 && ledState[i].B == 0)) {
          ledState[i] = color; // Aktualisiere den LED-Zustand nur bei hoher Priorität oder wenn die LED ausgeschaltet ist
          strip.SetPixelColor(i, ledState[i]);
        }
      } else {
        Serial.printf("WARNUNG: LED-Index %d außerhalb des Bereichs!\n", i);
      }
    }
  }
}


// Löscht alle LEDs und setzt das ledState-Array zurück
void clearLEDs() {
  for(int i = 0; i < NUM_LEDS; i++) {
    ledState[i] = RgbColor(0, 0, 0);
    strip.SetPixelColor(i, ledState[i]);
  }
  Serial.println("Alle LEDs wurden ausgeschaltet.");
}

// Globale Variable zur Verfolgung der letzten Aktualisierung der manuellen Zeit
unsigned long lastManualUpdateMillis = 0;

// Initialisierungsflag
bool initialized = false;

// Funktion zur Umwandlung von HSV in RGB
RgbColor hsvToRgb(uint16_t hue, uint8_t saturation, uint8_t value) {
  uint8_t region = hue / 60;
  uint8_t remainder = hue % 60;

  uint8_t p = (value * (255 - saturation)) / 255;
  uint8_t q = (value * (255 - ((saturation * remainder) / 60))) / 255;
  uint8_t t = (value * (255 - ((saturation * (60 - remainder)) / 60))) / 255;

  switch (region) {
    case 0:
      return RgbColor(value, t, p);
    case 1:
      return RgbColor(q, value, p);
    case 2:
      return RgbColor(p, value, t);
    case 3:
      return RgbColor(p, q, value);
    case 4:
      return RgbColor(t, p, value);
    default:
      return RgbColor(value, p, q);
  }
}

/// Setzt ein Wort basierend auf den WordIndices unter Berücksichtigung des LED-Zustands
void setWord(WordIndices word, RgbColor color) {
  Serial.printf("Setze Wort von LED %d bis %d mit Farbe RGB(%d, %d, %d)\n", word.start, word.end, color.R, color.G, color.B);
  if (word.start <= word.end) {
    for (int i = word.start; i <= word.end; i++) {
      if (i >= 0 && i < NUM_LEDS) {
        ledState[i] = color; // Aktualisiere den LED-Zustand
        strip.SetPixelColor(i, ledState[i]);
      } else {
        Serial.printf("WARNUNG: LED-Index %d außerhalb des Bereichs!\n", i);
      }
    }
  } else {
    for (int i = word.start; i >= word.end; i--) {
      if (i >= 0 && i < NUM_LEDS) {
        ledState[i] = color; // Aktualisiere den LED-Zustand
        strip.SetPixelColor(i, ledState[i]);
      } else {
        Serial.printf("WARNUNG: LED-Index %d außerhalb des Bereichs!\n", i);
      }
    }
  }
}

// Setzt ein einzelnes LED unter Berücksichtigung des LED-Zustands
void setSingleLED(int index, RgbColor color) {
  if (index >= 0 && index < NUM_LEDS) {
    ledState[index] = color; // Aktualisiere den LED-Zustand
    strip.SetPixelColor(index, ledState[index]);
    Serial.printf("Setze einzelne LED %d mit Farbe RGB(%d, %d, %d)\n", index, color.R, color.G, color.B);
  } else {
    Serial.printf("WARNUNG: Einzel-LED-Index %d außerhalb des Bereichs!\n", index);
  }
}



// Funktion zum Starten des Access Points im AP+STA Modus
void startAccessPoint() {
  Serial.println("Starte Access Point als dauerhafte Einrichtung...");
  WiFi.mode(WIFI_AP_STA); // Aktiviert sowohl AP als auch STA Modus
  bool apStarted = WiFi.softAP("ClockAP", "password123", 1, false, 10); // SSID, Passwort, Kanal, unsichtbar, max_connection=10
  if (apStarted) {
    IPAddress apIP = WiFi.softAPIP();
    Serial.println("Access Point erfolgreich gestartet.");
    Serial.print("AP IP-Adresse: ");
    Serial.println(apIP);
  } else {
    Serial.println("ERROR: Access Point konnte nicht gestartet werden.");
  }
}

// Funktion zum Speichern eines Tagesplans
void saveDaySchedule(int day, DaySchedule schedule) {
  if(day < 0 || day > 6) return; // Gültigkeitsprüfung
  
  String keyStartHour = "day_" + String(day) + "_startHour";
  String keyStartMinute = "day_" + String(day) + "_startMinute";
  String keyEndHour = "day_" + String(day) + "_endHour";
  String keyEndMinute = "day_" + String(day) + "_endMinute";
  String keyDimmed = "day_" + String(day) + "_dimmed";
  
  preferences.putInt(keyStartHour.c_str(), schedule.startHour);
  preferences.putInt(keyStartMinute.c_str(), schedule.startMinute);
  preferences.putInt(keyEndHour.c_str(), schedule.endHour);
  preferences.putInt(keyEndMinute.c_str(), schedule.endMinute);
  preferences.putBool(keyDimmed.c_str(), schedule.dimmed);
}

// Funktion zum Laden der Tagespläne
void loadDaySchedules() {
  for(int day = 0; day < 7; day++) {
    String keyStartHour = "day_" + String(day) + "_startHour";
    String keyStartMinute = "day_" + String(day) + "_startMinute";
    String keyEndHour = "day_" + String(day) + "_endHour";
    String keyEndMinute = "day_" + String(day) + "_endMinute";
    String keyDimmed = "day_" + String(day) + "_dimmed";
    
    weekSchedules[day].startHour = preferences.getInt(keyStartHour.c_str(), 8); // Standard: 08:00
    weekSchedules[day].startMinute = preferences.getInt(keyStartMinute.c_str(), 0);
    weekSchedules[day].endHour = preferences.getInt(keyEndHour.c_str(), 22); // Standard: 22:00
    weekSchedules[day].endMinute = preferences.getInt(keyEndMinute.c_str(), 0);
    weekSchedules[day].dimmed = preferences.getBool(keyDimmed.c_str(), false);
    
    Serial.printf("Wochentag %d geladen: Start %02d:%02d, Ende %02d:%02d, Dimmed: %s\n", 
                  day, 
                  weekSchedules[day].startHour, weekSchedules[day].startMinute,
                  weekSchedules[day].endHour, weekSchedules[day].endMinute,
                  weekSchedules[day].dimmed ? "Ja" : "Nein");
  }
}

// Funktion zur Anzeige von Datum und Wochentag (optional)
void displayDateAndWeekday(struct tm timeInfo) {
  // Implementiere die Anzeige von Datum und Wochentag hier
  // Diese Funktion kann erweitert werden, wenn gewünscht
}
// Handler für die Startseite
void handleRoot() {
  Serial.println("Anfrage an handleRoot empfangen.");
  String html = "<!DOCTYPE html><html><head><title>Word Clock</title>";
  html += "<meta charset=\"UTF-8\">"; // Sicherstellen, dass UTF-8 verwendet wird
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; }";
  html += "input[type='text'], input[type='password'], input[type='number'], input[type='color'] { width: 100%; padding: 12px; margin: 8px 0; }";
  html += "input[type='submit'] { width: 100%; padding: 12px; background-color: #4CAF50; color: white; border: none; }";
  html += ".switch { position: relative; display: inline-block; width: 60px; height: 34px; }";
  html += ".switch input { opacity: 0; width: 0; height: 0; }";
  html += ".slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; }";
  html += ".slider:before { position: absolute; content: \"\"; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: white; transition: .4s; }";
  html += "input:checked + .slider { background-color: #2196F3; }";
  html += "input:focus + .slider { box-shadow: 0 0 1px #2196F3; }";
  html += "input:checked + .slider:before { transform: translateX(26px); }";
  html += ".slider.round { border-radius: 34px; }";
  html += ".slider.round:before { border-radius: 50%; }";
  html += "</style>";
  html += "</head><body>";
  html += "<h1>Word Clock Konfiguration</h1>";

  // Versionsinformationen, Datum und Ersteller hinzufügen
  html += "<p><strong>Version:</strong> 1.9.3<br>";
  html += "<strong>Datum:</strong> 12.2024<br>";
  html += "<strong>Ersteller:</strong> Morpheus2510</p>";

  html += "<p><a href=\"/reset\" onclick=\"return confirm('Möchten Sie das Gerät wirklich zurücksetzen? Alle Einstellungen gehen verloren.');\">Gerät zurücksetzen</a></p>";

  // Aktuelle Uhrzeit anzeigen
  struct tm timeInfo;
  bool timeValid = false;

  if (useManualTime) {
    timeInfo = manualTime;
    // Anwenden des Zeit-Offsets
    timeInfo.tm_min += timeOffsetMinutes;
    mktime(&timeInfo); // Normalisiert die Zeitstruktur
    timeValid = true;
    char manualTimeStr[25];
    sprintf(manualTimeStr, "%04d-%02d-%02d %02d:%02d:%02d",
            timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
            timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
    html += "<p>Aktuelle Uhrzeit (Manuell): " + String(manualTimeStr) + "</p>";
  }
  else {
    if (getLocalTime(&timeInfo)) {
      // Anwenden des Zeit-Offsets
      timeInfo.tm_min += timeOffsetMinutes;
      mktime(&timeInfo); // Normalisiert die Zeitstruktur
      timeValid = true;
      char timeStr[16];
      sprintf(timeStr, "%02d:%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
      html += "<p>Aktuelle Uhrzeit (Online): " + String(timeStr) + "</p>";
    } else {
      html += "<p>Aktuelle Uhrzeit: Nicht verfügbar</p>";
    }
  }

  // Lokale IP-Adresse anzeigen
  html += "<p>Lokale IP-Adresse: " + WiFi.localIP().toString() + "</p>";

  // Navigationslinks zur Konfiguration
  html += "<p><a href=\"/wifi\">Wi-Fi Einstellungen</a></p>";
  html += "<p><a href=\"/set_manual_time\">Manuelle Zeit einstellen</a></p>";
  html += "<p><a href=\"/toggle_mode\">Modus wechseln (Aktuell: " + String(useManualTime ? "Offline" : "Online") + ")</a></p>";
  html += "<p><a href=\"/set_time_offset\">Zeit-Offset einstellen</a></p>";
  
  // Neuer Link für Tagespläne
  html += "<p><a href=\"/configure_day_schedules\">Tagespläne einstellen</a></p>";

  // Erklärung zum Dimmed-Modus
  html += "<p><em>Hinweis:</em> Wenn der Dimmed-Modus nicht aktiviert ist, werden die LEDs komplett ausgeschaltet.</p>";

  // Farbe, Helligkeit und Regenbogenmodus einstellen
  html += "<form action=\"/configure_main\" method=\"post\">";
  html += "Farbe:<br> <input type=\"color\" name=\"color\" value=\"#";
  char colorStrFinal[7];
  sprintf(colorStrFinal, "%02X%02X%02X", selectedColor.R, selectedColor.G, selectedColor.B);
  html += String(colorStrFinal) + "\"><br>";

  html += "Helligkeit (0-255):<br> <input type=\"number\" name=\"brightness\" min=\"0\" max=\"255\" value=\"" + String(brightness) + "\"><br>";

  // Regenbogenmodus Checkbox
  html += "Regenbogenmodus:<br>";
  html += "<label class=\"switch\">";
  html += "<input type=\"checkbox\" name=\"rainbow\" " + String(rainbowMode ? "checked" : "") + ">";
  html += "<span class=\"slider round\"></span>";
  html += "</label><br><br>";

  html += "<input type=\"submit\" value=\"Speichern\">";
  html += "</form></body></html>";
  server.send(200, "text/html", html);
}


void handleUpdatePage() {
  String html = "<!DOCTYPE html><html><head><title>Online Update</title></head><body>";
  html += "<h1>Online Update</h1>";
  html += "<form action=\"/set_firmware_url\" method=\"post\">";
  html += "Firmware-URL:<br><input type=\"text\" name=\"firmwareurl\" value=\"" + firmwareURL + "\" style=\"width:100%;\"><br><br>";
  html += "<input type=\"submit\" value=\"URL speichern\"><br><br>";
  html += "</form>";
  
  // Button für sofortiges Update mit aktueller URL
  html += "<p>Aktuelle URL: " + firmwareURL + "</p>";
  html += "<p><a href=\"/do_update\">Jetzt Update einspielen</a></p>";
  
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// Handler für die Hauptkonfiguration (Farbe, Helligkeit, Regenbogenmodus)
void handleConfigureMain() {
  Serial.println("Anfrage an handleConfigureMain empfangen.");

  bool updateRainbow = false; // Flag, um festzustellen, ob der Regenbogenmodus geändert wurde

  // Farbe aktualisieren
  if (server.hasArg("color")) {
    String color = server.arg("color");
    long number = strtol(&color[1], NULL, 16);
    selectedColor = RgbColor((number >> 16) & 0xFF, (number >> 8) & 0xFF, number & 0xFF);
    // Farbe speichern
    uint32_t colorValue = ((uint32_t)selectedColor.R << 16) | ((uint32_t)selectedColor.G << 8) | selectedColor.B;
    preferences.putUInt("color", colorValue);
    Serial.println("Farbe aktualisiert und gespeichert.");
  }

  // Helligkeit aktualisieren
  if (server.hasArg("brightness")) {
    brightness = server.arg("brightness").toInt();
    strip.SetBrightness(brightness);
    preferences.putUChar("brightness", brightness);
    Serial.println("Helligkeit aktualisiert und gespeichert.");
  }

  // Regenbogenmodus aktualisieren
  bool newRainbowMode = server.hasArg("rainbow");
  if (rainbowMode != newRainbowMode) {
    rainbowMode = newRainbowMode;
    preferences.putBool("rainbow", rainbowMode);
    Serial.println("Regenbogenmodus aktualisiert und gespeichert.");
    updateRainbow = true;
  }

 

  // Einstellungen zurück auf die Startseite umleiten
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}
// Handler für das Zurücksetzen des Geräts
void handleReset() {
  Serial.println("Anfrage an handleReset empfangen.");
  
  // Sende eine Bestätigung an den Benutzer
  String html = "<!DOCTYPE html><html><head><title>Gerät zurücksetzen</title>";
  html += "<meta charset=\"UTF-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "</head><body>";
  html += "<h1>Gerät zurücksetzen</h1>";
  html += "<p>Möchten Sie das Gerät wirklich zurücksetzen? Alle Einstellungen, einschließlich der WLAN-Daten, gehen verloren.</p>";
  html += "<form action=\"/confirm_reset\" method=\"post\">";
  html += "<input type=\"submit\" value=\"Ja, Gerät zurücksetzen\">";
  html += "</form>";
  html += "<p><a href=\"/\">Abbrechen und zurück zur Startseite</a></p>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

// Handler für die Bestätigung des Resets
void handleConfirmReset() {
  Serial.println("Gerät wird zurückgesetzt...");
  
  // WLAN-Anmeldedaten löschen
  WiFi.disconnect(true, true); // Entfernt die gespeicherten WLAN-Daten aus dem Flash-Speicher
  
  // Preferences löschen
  preferences.clear(); // Löscht alle gespeicherten Präferenzen
  
  // Bestätigungsnachricht an den Benutzer senden
  server.send(200, "text/plain", "Das Gerät wird zurückgesetzt und neu gestartet...");
  
  delay(2000); // Kurze Verzögerung, um die Nachricht zu senden
  
  // Gerät neu starten
  ESP.restart();
}


// Handler für die Wi-Fi-Konfigurationsseite
void handleWifi() {
  Serial.println("Anfrage an handleWifi empfangen.");
  String html = "<!DOCTYPE html><html><head><title>Wi-Fi Einstellungen</title>";
  html += "<meta charset=\"UTF-8\">"; // Sicherstellen, dass UTF-8 verwendet wird
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; }";
  html += "input[type='text'], input[type='password'], select { width: 100%; padding: 12px; margin: 8px 0; }";
  html += "input[type='submit'] { width: 100%; padding: 12px; background-color: #4CAF50; color: white; border: none; }";
  html += "</style>";
  html += "</head><body>";
  html += "<h1>Wi-Fi Konfiguration</h1>";

  // Überprüfe den Status-Parameter
  if (server.hasArg("status")) {
    String status = server.arg("status");
    if (status == "success") {
      html += "<p style=\"color: green;\">Verbindung zum WLAN erfolgreich hergestellt!</p>";
    } else if (status == "failed") {
      html += "<p style=\"color: red;\">Verbindung zum WLAN fehlgeschlagen. Bitte überprüfe die Zugangsdaten und versuche es erneut.</p>";
    }
  }

  html += "<form action=\"/configure_wifi\" method=\"post\">";

  // WLAN-Netzwerke scannen
  int n = WiFi.scanNetworks();
  if (n == 0) {
    html += "<p>Keine WLAN-Netzwerke gefunden.</p>";
  } else {
    html += "<label for=\"ssid\">Verfügbare WLAN-Netzwerke:</label><br>";
    html += "<select name=\"ssid\" id=\"ssid\">";
    for (int i = 0; i < n; ++i) {
      // Sonderzeichen maskieren
      String ssidItem = WiFi.SSID(i);
      ssidItem.replace("\"", "&quot;");
      html += "<option value=\"" + ssidItem + "\">" + ssidItem + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
    }
    html += "</select><br>";
  }

  html += "<label for=\"password\">WLAN Passwort:</label><br>";
  html += "<input type=\"password\" id=\"password\" name=\"password\"><br>";

  // Navigationslink zurück zur Startseite
  html += "<p><a href=\"/\">Zurück zur Startseite</a></p>";

  html += "<input type=\"submit\" value=\"Speichern\">";
  html += "</form></body></html>";
  server.send(200, "text/html", html);
}

// Handler für die Wi-Fi-Konfiguration speichern
void handleConfigureWifi() {
  Serial.println("Anfrage an handleConfigureWifi empfangen.");
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  // WLAN-Daten speichern
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  Serial.println("WLAN-Daten gespeichert.");

  // Mit neuem WLAN verbinden
  Serial.print("Versuche, mit WLAN zu verbinden: ");
  Serial.println(ssid);
  WiFi.begin(ssid.c_str(), password.c_str());

  int retry = 0;
  const int maxRetries = 40; // 40 * 500ms = 20 Sekunden
  bool connected = false;
  while (WiFi.status() != WL_CONNECTED && retry < maxRetries) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nVerbunden mit WLAN!");
    Serial.print("IP-Adresse: ");
    Serial.println(WiFi.localIP());

    // Zeitzone für Deutschland einstellen
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
    Serial.println("Zeitzone konfiguriert.");

    // Warten auf Zeitsynchronisation
    struct tm timeInfo;
    int retryTime = 0;
    while (!getLocalTime(&timeInfo) && retryTime < 10) {
      Serial.println("Warte auf Zeitsynchronisation...");
      delay(1000);
      retryTime++;
    }
    if (getLocalTime(&timeInfo)) {
      Serial.println("Zeit erfolgreich synchronisiert.");
      // Automatisches Umschalten in den Online-Modus, falls noch nicht geschehen
      if (useManualTime) {
        useManualTime = false;
        preferences.putBool("useManualTime", useManualTime);
        Serial.println("Internetverbindung hergestellt. Wechsel zu Online-Modus.");

        // Zeit synchronisieren
        manualTime = timeInfo;
        Serial.println("Zeit synchronisiert nach Wechsel zu Online-Modus.");
      }
      connected = true;
    } else {
      Serial.println("Zeit konnte nicht synchronisiert werden.");
    }
  } else {
    Serial.println("\nVerbindung mit WLAN fehlgeschlagen.");
  }

  // Bestimmung des Status für die Webseite
  String status;
  if (connected) {
    status = "success";
  } else {
    status = "failed";
  }

  // Nach der Konfiguration zurück zur Wi-Fi Seite umleiten mit Status
  server.sendHeader("Location", "/wifi?status=" + status, true);
  server.send(302, "text/plain", "");
}

// Handler für das Setzen der manuellen Zeit
void handleSetManualTime() {
  Serial.println("Anfrage an handleSetManualTime empfangen.");
  if (server.method() == HTTP_GET) {
    // Anzeige des Formulars zur manuellen Zeiteinstellung
    String html = "<!DOCTYPE html><html><head><title>Manuelle Zeit einstellen</title>";
    html += "<meta charset=\"UTF-8\">"; // Sicherstellen, dass UTF-8 verwendet wird
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; margin: 20px; }";
    html += "input[type='number'] { width: 100%; padding: 12px; margin: 8px 0; }";
    html += "input[type='submit'] { width: 100%; padding: 12px; background-color: #4CAF50; color: white; border: none; }";
    html += "</style>";
    html += "</head><body>";
    html += "<h1>Manuelle Zeit einstellen</h1>";
    html += "<form action=\"/set_manual_time\" method=\"post\">";
    html += "Jahr (z.B., 2023):<br> <input type=\"number\" name=\"year\" min=\"2000\" max=\"2100\" required><br>";
    html += "Monat (1-12):<br> <input type=\"number\" name=\"month\" min=\"1\" max=\"12\" required><br>";
    html += "Tag (1-31):<br> <input type=\"number\" name=\"day\" min=\"1\" max=\"31\" required><br>";
    html += "Stunde (0-23):<br> <input type=\"number\" name=\"hour\" min=\"0\" max=\"23\" required><br>";
    html += "Minute (0-59):<br> <input type=\"number\" name=\"minute\" min=\"0\" max=\"59\" required><br>";
    html += "Sekunde (0-59):<br> <input type=\"number\" name=\"second\" min=\"0\" max=\"59\" required><br><br>";
    html += "<input type=\"submit\" value=\"Speichern\">";
    html += "</form>";
    html += "<p><a href=\"/\">Zurück zur Startseite</a></p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  }
  else if (server.method() == HTTP_POST) {
    // Verarbeiten der manuellen Zeiteingaben
    if (server.hasArg("year") && server.hasArg("month") && server.hasArg("day") &&
        server.hasArg("hour") && server.hasArg("minute") && server.hasArg("second")) {
      manualTime.tm_year = server.arg("year").toInt() - 1900;
      manualTime.tm_mon = server.arg("month").toInt() - 1;
      manualTime.tm_mday = server.arg("day").toInt();
      manualTime.tm_hour = server.arg("hour").toInt();
      manualTime.tm_min = server.arg("minute").toInt();
      manualTime.tm_sec = server.arg("second").toInt();

      // Speichern der manuellen Zeit
      preferences.putInt("manual_tm_year", manualTime.tm_year + 1900);
      preferences.putInt("manual_tm_mon", manualTime.tm_mon + 1);
      preferences.putInt("manual_tm_mday", manualTime.tm_mday);
      preferences.putInt("manual_tm_hour", manualTime.tm_hour);
      preferences.putInt("manual_tm_min", manualTime.tm_min);
      preferences.putInt("manual_tm_sec", manualTime.tm_sec);

      // Setzen des Offline-Modus
      useManualTime = true;
      preferences.putBool("useManualTime", useManualTime);
      Serial.println("Manuelle Zeit gespeichert und Offline-Modus aktiviert.");

      // Initialisiere lastManualUpdateMillis
      lastManualUpdateMillis = millis();

      // Weiterleitung zur Startseite
      server.sendHeader("Location", "/", true);
      server.send(302, "text/plain", "");
    }
    else {
      server.send(400, "text/plain", "Ungültige Anfrage");
    }
  }
}

// Handler zum Umschalten zwischen Online- und Offline-Modus
void handleToggleMode() {
  Serial.println("Anfrage an handleToggleMode empfangen.");
  if (useManualTime) {
    useManualTime = false;
    Serial.println("Offline-Modus deaktiviert. Wechsel zu Online-Modus.");
  }
  else {
    useManualTime = true;
    Serial.println("Online-Modus deaktiviert. Wechsel zu Offline-Modus.");
  }
  preferences.putBool("useManualTime", useManualTime);

  // Weiterleitung zur Startseite
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// Handler zum Einstellen des Zeit-Offsets
void handleSetTimeOffset() {
  Serial.println("Anfrage an handleSetTimeOffset empfangen.");
  if (server.method() == HTTP_GET) {
    // Anzeige des Formulars zur Einstellung des Zeit-Offsets
    String html = "<!DOCTYPE html><html><head><title>Zeit-Offset einstellen</title>";
    html += "<meta charset=\"UTF-8\">"; // Sicherstellen, dass UTF-8 verwendet wird
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; margin: 20px; }";
    html += "input[type='number'] { width: 100%; padding: 12px; margin: 8px 0; }";
    html += "input[type='submit'] { width: 100%; padding: 12px; background-color: #4CAF50; color: white; border: none; }";
    html += "</style>";
    html += "</head><body>";
    html += "<h1>Zeit-Offset einstellen</h1>";
    html += "<form action=\"/set_time_offset\" method=\"post\">";
    html += "Zeit-Offset in Minuten (z.B., -5 oder +5):<br> <input type=\"number\" name=\"offset\" min=\"-60\" max=\"60\" required><br><br>";
    html += "<input type=\"submit\" value=\"Speichern\">";
    html += "</form>";
    html += "<p><a href=\"/\">Zurück zur Startseite</a></p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  }
  else if (server.method() == HTTP_POST) {
    // Verarbeiten des Zeit-Offsets
    if (server.hasArg("offset")) {
      timeOffsetMinutes = server.arg("offset").toInt();
      preferences.putInt("timeOffset", timeOffsetMinutes);
      Serial.printf("Zeit-Offset gesetzt: %d Minuten\n", timeOffsetMinutes);

      // Weiterleitung zur Startseite
      server.sendHeader("Location", "/", true);
      server.send(302, "text/plain", "");
    }
    else {
      server.send(400, "text/plain", "Ungültige Anfrage");
    }
  }
}

// Handler für die Tagesplan-Konfigurationsseite
void handleConfigureDaySchedules() {
  Serial.println("Anfrage an handleConfigureDaySchedules empfangen.");
  
  if(server.method() == HTTP_GET) {
    // Anzeige des Formulars zur Einstellung der Tagespläne
    String html = "<!DOCTYPE html><html><head><title>Tagespläne einstellen</title>";
    html += "<meta charset=\"UTF-8\">"; // Sicherstellen, dass UTF-8 verwendet wird
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; margin: 20px; }";
    html += "input[type='number'] { width: 60px; padding: 8px; margin: 4px; }";
    html += "input[type='checkbox'] { margin-left: 10px; }";
    html += "input[type='submit'] { padding: 10px 20px; background-color: #4CAF50; color: white; border: none; cursor: pointer; }";
    html += "</style>";
    html += "</head><body>";
    html += "<h1>Tagespläne einstellen</h1>";
    
    // Erklärung hinzufügen
    html += "<p><em>Hinweis:</em> Wenn der Dimmed-Modus nicht aktiviert ist, werden die LEDs komplett ausgeschaltet.</p>";
    
    html += "<form action=\"/configure_day_schedules\" method=\"post\">";
    
    // Schleife über alle Wochentage
    const char* weekDays[7] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
    
    for(int day = 0; day < 7; day++) {
      html += "<h3>" + String(weekDays[day]) + "</h3>";
      html += "Startzeit: ";
      html += "<input type=\"number\" name=\"day_" + String(day) + "_startHour\" min=\"0\" max=\"23\" value=\"" + String(weekSchedules[day].startHour) + "\">:";
      html += "<input type=\"number\" name=\"day_" + String(day) + "_startMinute\" min=\"0\" max=\"59\" value=\"" + String(weekSchedules[day].startMinute) + "\"><br>";
      
      html += "Endzeit: ";
      html += "<input type=\"number\" name=\"day_" + String(day) + "_endHour\" min=\"0\" max=\"23\" value=\"" + String(weekSchedules[day].endHour) + "\">:";
      html += "<input type=\"number\" name=\"day_" + String(day) + "_endMinute\" min=\"0\" max=\"59\" value=\"" + String(weekSchedules[day].endMinute) + "\">";
      
      html += " Dimmed: <input type=\"checkbox\" name=\"day_" + String(day) + "_dimmed\" " + String(weekSchedules[day].dimmed ? "checked" : "") + "><br><br>";
    }
    
    html += "<input type=\"submit\" value=\"Speichern\">";
    html += "</form>";
    
    html += "<p><a href=\"/\">Zurück zur Startseite</a></p>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
  }
  else if(server.method() == HTTP_POST) {
    // Verarbeiten der eingereichten Formulardaten
    for(int day = 0; day < 7; day++) {
      if(server.hasArg("day_" + String(day) + "_startHour") &&
         server.hasArg("day_" + String(day) + "_startMinute") &&
         server.hasArg("day_" + String(day) + "_endHour") &&
         server.hasArg("day_" + String(day) + "_endMinute")) {
        
        weekSchedules[day].startHour = server.arg("day_" + String(day) + "_startHour").toInt();
        weekSchedules[day].startMinute = server.arg("day_" + String(day) + "_startMinute").toInt();
        weekSchedules[day].endHour = server.arg("day_" + String(day) + "_endHour").toInt();
        weekSchedules[day].endMinute = server.arg("day_" + String(day) + "_endMinute").toInt();
        weekSchedules[day].dimmed = server.hasArg("day_" + String(day) + "_dimmed");
        
        // Speichere die Einstellungen
        saveDaySchedule(day, weekSchedules[day]);
      }
    }
    
    Serial.println("Tagespläne gespeichert.");
    
    // Weiterleitung zur Startseite
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  }
}

enum WordIndex {
  WORD_ES = 0,
  WORD_IST,
  WORD_EIN,
  WORD_EINS,
  WORD_ZWEI,
  WORD_DREI,
  WORD_VIER,
  WORD_FUENF,
  WORD_SECHS,
  WORD_SIEBEN,
  WORD_ACHT,
  WORD_NEUN,
  WORD_ZEHN,
  WORD_ELF,
  WORD_ZWOELF,
  WORD_VOR,
  WORD_NACH,
  WORD_HALB,
  WORD_UHR,
  WORD_MINUTE,     // "Minute"
  WORD_MINUTEN,    // "Minuten"
  WORD_FUENF_M,    // "Fünf" für Minuten
  WORD_ZEHN_M,     // "Zehn" für Minuten
  WORD_VIERTEL,
  WORD_ZWANZIG,
  WORD_PLUS,       // '+'-Symbol
  WORD_NUM_1,      // Zahl '1'
  WORD_NUM_2,      // Zahl '2'
  WORD_NUM_3,      // Zahl '3'
  WORD_NUM_4,      // Zahl '4'
  NUM_WORDS        // Gesamtzahl der Wörter
};



// Array zur Speicherung der Hue-Werte für jedes Wort
uint16_t wordHues[NUM_WORDS];


// Funktion zur Initialisierung der Hue-Werte
void initializeWordHues() {
  for(int i = 0; i < NUM_WORDS; i++) {
    wordHues[i] = random(0, 360);
    Serial.printf("Initial Hue für Wort %d: %d\n", i, wordHues[i]);
  }
}



RgbColor getColorForWord(int wordIndex) {
  RgbColor color;
  if (rainbowMode) {
    if (wordIndex == WORD_MINUTEN) {
      color = hsvToRgb(wordHues[WORD_MINUTE], 255, 255); // "Minuten" benutzt den Hue von "Minute"
      Serial.printf("Farbe für WORD_MINUTEN (%d): RGB(%d, %d, %d)\n", wordIndex, color.R, color.G, color.B);
    }
    else {
      color = hsvToRgb(wordHues[wordIndex], 255, 255); // Andere Wörter wie gewohnt
      Serial.printf("Farbe für Wort %d: RGB(%d, %d, %d)\n", wordIndex, color.R, color.G, color.B);
    }
  }
  else {
    color = selectedColor;
    Serial.printf("Farbe für Wort %d: RGB(%d, %d, %d)\n", wordIndex, color.R, color.G, color.B);
  }
  return color;
}


void displayTime() {  
  static int lastMinute = -1; // Variable zur Speicherung der letzten angezeigten Minute

  clearLEDs(); // LEDs und ledState-Array zurücksetzen

  // Aktuelle Zeit abrufen
  struct tm timeInfo;
  bool timeValid = false;

  if (useManualTime) {
    // Verwenden der manuellen Zeit
    unsigned long currentMillis = millis();
    unsigned long elapsedMillis = currentMillis - lastManualUpdateMillis;

    if (elapsedMillis >= 1000) { // Mindestens eine Sekunde vergangen
      unsigned long secondsToAdd = elapsedMillis / 1000;
      lastManualUpdateMillis += secondsToAdd * 1000;

      // Inkrementiere die manuelle Zeit um die verstrichenen Sekunden
      manualTime.tm_sec += secondsToAdd;

      // Normalisiert die Zeitstruktur (übergeht Minuten, Stunden, etc.)
      mktime(&manualTime);
    }

    // Setze die ZeitInfo basierend auf manualTime
    timeInfo = manualTime;

    // Anwenden des Zeit-Offsets
    timeInfo.tm_min += timeOffsetMinutes;
    mktime(&timeInfo); // Normalisiert die Zeitstruktur
    timeValid = true;
    Serial.println("Offline-Modus: Verwende manuelle Zeit.");
  }
  else {
    // Verwenden der NTP-Zeit
    if (getLocalTime(&timeInfo)) {
      // Anwenden des Zeit-Offsets
      timeInfo.tm_min += timeOffsetMinutes;
      mktime(&timeInfo); // Normalisiert die Zeitstruktur
      timeValid = true;
      Serial.println("Online-Modus: Verwende NTP-Zeit.");
    }
    else {
      Serial.println("Fehler beim Abrufen der Zeit");
    }
  }

  // Debug: Zeige den angewendeten Zeit-Offset
  Serial.printf("Zeit-Offset angewendet: %d Minuten\n", timeOffsetMinutes);

  if (!timeValid) {
    Serial.println("Ungültige Zeit. LEDs ausgeschaltet.");
    return;
  }

  int currentHour = timeInfo.tm_hour;
  int currentMinute = timeInfo.tm_min;
  int currentWday = timeInfo.tm_wday; // 0 = Sonntag, 1 = Montag, ..., 6 = Samstag

  // Überprüfen Sie, ob sich die Minute geändert hat
  if (rainbowMode && currentMinute != lastMinute) {
    lastMinute = currentMinute;

    // Aktualisieren Sie die Hue-Werte der Wörter
    for(int i = 0; i < NUM_WORDS; i++) {
      wordHues[i] = random(0, 360);
    }
    Serial.println("Farben der Wörter wurden aktualisiert.");
  }

  // Überprüfe den Tagesplan für den aktuellen Wochentag
  DaySchedule currentSchedule = weekSchedules[currentWday];

  // Konvertiere die aktuelle Zeit und die Start-/Endzeiten in Minuten seit Mitternacht
  int currentTimeInMinutes = currentHour * 60 + currentMinute;
  int startTimeInMinutes = currentSchedule.startHour * 60 + currentSchedule.startMinute;
  int endTimeInMinutes = currentSchedule.endHour * 60 + currentSchedule.endMinute;

  bool isDaytime = false;
  bool isDimmed = false;

  if(startTimeInMinutes < endTimeInMinutes) {
    // Zeitspanne innerhalb eines Tages
    if(currentTimeInMinutes >= startTimeInMinutes && currentTimeInMinutes < endTimeInMinutes) {
      isDaytime = true;
    }
  }
  else {
    // Zeitspanne über Mitternacht hinaus
    if(currentTimeInMinutes >= startTimeInMinutes || currentTimeInMinutes < endTimeInMinutes) {
      isDaytime = true;
    }
  }

  if(isDaytime) {
    // Standardmodus: volle Helligkeit
    strip.SetBrightness(brightness);
    Serial.println("Standardmodus aktiviert: LEDs voll leuchtend.");
  }
  else {
    if(currentSchedule.dimmed) {
      // Dimmed-Modus: reduzierte Helligkeit
      strip.SetBrightness(brightness / 4); // Beispiel: 25% der normalen Helligkeit
      Serial.println("Dimmed-Modus aktiviert: LEDs gedimmt.");
    }
    else {
      // LEDs ausschalten
      strip.SetBrightness(0);
      Serial.println("Nachtmodus aktiviert: LEDs ausgeschaltet.");
    }
  }

  // Aktuelle Zeit im seriellen Monitor ausgeben
  Serial.printf("Aktuelle Zeit: %02d:%02d, Wochentag: %d\n", currentHour, currentMinute, currentWday);

  // Anpassung der Stunde für 12-Stunden-Format
  int displayHour = currentHour % 12;
  if (displayHour == 0) displayHour = 12;
  int hourIndex = displayHour - 1;

  // Map für Stundenwörter zu Wortindizes
  int hourWordIndices[12] = {
    WORD_EIN,   // Index 0
    WORD_ZWEI,  // Index 1
    WORD_DREI,  // Index 2
    WORD_VIER,  // Index 3
    WORD_FUENF, // Index 4
    WORD_SECHS, // Index 5
    WORD_SIEBEN,// Index 6
    WORD_ACHT,  // Index 7
    WORD_NEUN,  // Index 8
    WORD_ZEHN,  // Index 9
    WORD_ELF,   // Index 10
    WORD_ZWOELF // Index 11
  };

  // Minuten verarbeiten
  if (currentMinute == 0) {
    // Ganze Stunde
    setWordWithPriority(ES, getColorForWord(WORD_ES), false); // "ES"
    setWordWithPriority(IST, getColorForWord(WORD_IST), false); // "IST"

    int wordIndex = hourWordIndices[hourIndex];
    setWordWithPriority(STUNDENADR[hourIndex], getColorForWord(wordIndex), true); // Stundenwort setzen mit hoher Priorität
    setWordWithPriority(HalfFullHourAddr[1], getColorForWord(WORD_UHR), false); // "UHR" setzen
    Serial.println("Anzeige: Es ist [Ganze Stunde]");
  }
  else if (currentMinute > 0 && currentMinute < 5) {
    // "Es ist [Stunde] Uhr +x Minuten"
    setWordWithPriority(ES, getColorForWord(WORD_ES), false); // "ES"
    setWordWithPriority(IST, getColorForWord(WORD_IST), false); // "IST"

    int wordIndex = hourWordIndices[hourIndex];
    setWordWithPriority(STUNDENADR[hourIndex], getColorForWord(wordIndex), true); // Stundenwort setzen mit hoher Priorität
    setWordWithPriority(HalfFullHourAddr[1], getColorForWord(WORD_UHR), false); // "UHR" setzen

    // '+'-Symbol einschalten
    setSingleLED(LED_PLUS, getColorForWord(WORD_PLUS));
    Serial.println("Zeige '+'-Symbol an.");

    // Alle Zahlen ausschalten
    setSingleLED(LED_1, RgbColor(0, 0, 0));
    setSingleLED(LED_2, RgbColor(0, 0, 0));
    setSingleLED(LED_3, RgbColor(0, 0, 0));
    setSingleLED(LED_4, RgbColor(0, 0, 0));

    // Zusätzliche Minutenanzeige einschalten
    switch(currentMinute) { // extraMinutes = currentMinute
      case 1:
        setSingleLED(LED_1, getColorForWord(WORD_NUM_1));
        setWordWithPriority(MINUTE, getColorForWord(WORD_MINUTE), false); // "Minute" setzen
        Serial.println("Zeige '1' an und setze 'Minute'.");
        break;
      case 2:
        setSingleLED(LED_2, getColorForWord(WORD_NUM_2));
        setWordWithPriority(MINUTE, getColorForWord(WORD_MINUTE), false); // "Minute" setzen
        setWordWithPriority(MINUTEN, getColorForWord(WORD_MINUTEN), true); // "Minuten" setzen mit hoher Priorität
        Serial.println("Zeige '2' an und setze 'Minute' sowie 'Minuten'.");
        break;
      case 3:
        setSingleLED(LED_3, getColorForWord(WORD_NUM_3));
        setWordWithPriority(MINUTE, getColorForWord(WORD_MINUTE), false); // "Minute" setzen
        setWordWithPriority(MINUTEN, getColorForWord(WORD_MINUTEN), true); // "Minuten" setzen mit hoher Priorität
        Serial.println("Zeige '3' an und setze 'Minute' sowie 'Minuten'.");
        break;
      case 4:
        setSingleLED(LED_4, getColorForWord(WORD_NUM_4));
        setWordWithPriority(MINUTE, getColorForWord(WORD_MINUTE), false); // "Minute" setzen
        setWordWithPriority(MINUTEN, getColorForWord(WORD_MINUTEN), true); // "Minuten" setzen mit hoher Priorität
        Serial.println("Zeige '4' an und setze 'Minute' sowie 'Minuten'.");
        break;
    }
  }
  else {
    // Berechne Basis und zusätzliche Minuten
    int baseMinutes = (currentMinute / 5) * 5;
    int extraMinutes = currentMinute % 5;

    Serial.printf("Basis-Minuten: %d\n", baseMinutes);
    Serial.printf("Zusätzliche Minuten: %d\n", extraMinutes);

    // Setze Basis-Minuten-Wörter
    switch(baseMinutes) {
      case 5: {
        setWordWithPriority(ES, getColorForWord(WORD_ES), false); // "ES"
        setWordWithPriority(IST, getColorForWord(WORD_IST), false); // "IST"
        setWordWithPriority(MINUTENADR[0], getColorForWord(WORD_FUENF_M), false); // "fünf"
        setWordWithPriority(ToPastAddr[1], getColorForWord(WORD_NACH), false); // "nach"
        break;
      }
      case 10: {
        setWordWithPriority(ES, getColorForWord(WORD_ES), false); // "ES"
        setWordWithPriority(IST, getColorForWord(WORD_IST), false); // "IST"
        setWordWithPriority(MINUTENADR[1], getColorForWord(WORD_ZEHN_M), false); // "zehn"
        setWordWithPriority(ToPastAddr[1], getColorForWord(WORD_NACH), false); // "nach"
        break;
      }
      case 15: {
        setWordWithPriority(ES, getColorForWord(WORD_ES), false); // "ES"
        setWordWithPriority(IST, getColorForWord(WORD_IST), false); // "IST"
        setWordWithPriority(MINUTENADR[2], getColorForWord(WORD_VIERTEL), false); // "viertel"
        setWordWithPriority(ToPastAddr[1], getColorForWord(WORD_NACH), false); // "nach"
        break;
      }
      case 20: {
        setWordWithPriority(ES, getColorForWord(WORD_ES), false); // "ES"
        setWordWithPriority(IST, getColorForWord(WORD_IST), false); // "IST"
        setWordWithPriority(MINUTENADR[3], getColorForWord(WORD_ZWANZIG), false); // "zwanzig"
        setWordWithPriority(ToPastAddr[1], getColorForWord(WORD_NACH), false); // "nach"
        break;
      }
      case 25: {
        setWordWithPriority(ES, getColorForWord(WORD_ES), false); // "ES"
        setWordWithPriority(IST, getColorForWord(WORD_IST), false); // "IST"
        setWordWithPriority(MINUTENADR[0], getColorForWord(WORD_FUENF_M), false); // "fünf"
        setWordWithPriority(ToPastAddr[0], getColorForWord(WORD_VOR), false); // "vor"
        setWordWithPriority(HalfFullHourAddr[0], getColorForWord(WORD_HALB), false); // "halb"
        displayHour = (currentHour % 12) + 1; // Nächste Stunde
        if (displayHour == 13) displayHour = 1;
        hourIndex = displayHour - 1;
        break;
      }
      case 30: {
        setWordWithPriority(ES, getColorForWord(WORD_ES), false); // "ES"
        setWordWithPriority(IST, getColorForWord(WORD_IST), false); // "IST"
        setWordWithPriority(HalfFullHourAddr[0], getColorForWord(WORD_HALB), false); // "halb"
        displayHour = (currentHour % 12) + 1; // Nächste Stunde
        if (displayHour == 13) displayHour = 1;
        hourIndex = displayHour - 1;
        break;
      }
      case 35: {
        setWordWithPriority(ES, getColorForWord(WORD_ES), false); // "ES"
        setWordWithPriority(IST, getColorForWord(WORD_IST), false); // "IST"
        setWordWithPriority(MINUTENADR[0], getColorForWord(WORD_FUENF_M), false); // "fünf"
        setWordWithPriority(ToPastAddr[1], getColorForWord(WORD_NACH), false); // "nach"
        setWordWithPriority(HalfFullHourAddr[0], getColorForWord(WORD_HALB), false); // "halb"
        displayHour = (currentHour % 12) + 1; // Nächste Stunde
        if (displayHour == 13) displayHour = 1;
        hourIndex = displayHour - 1;
        break;
      }
      case 40: {
        setWordWithPriority(ES, getColorForWord(WORD_ES), false); // "ES"
        setWordWithPriority(IST, getColorForWord(WORD_IST), false); // "IST"
        setWordWithPriority(MINUTENADR[3], getColorForWord(WORD_ZWANZIG), false); // "zwanzig"
        setWordWithPriority(ToPastAddr[0], getColorForWord(WORD_VOR), false); // "vor"
        displayHour = (currentHour % 12) + 1; // Nächste Stunde
        if (displayHour == 13) displayHour = 1;
        hourIndex = displayHour - 1;
        break;
      }
      case 45: {
        setWordWithPriority(ES, getColorForWord(WORD_ES), false); // "ES"
        setWordWithPriority(IST, getColorForWord(WORD_IST), false); // "IST"
        setWordWithPriority(MINUTENADR[2], getColorForWord(WORD_VIERTEL), false); // "viertel"
        setWordWithPriority(ToPastAddr[0], getColorForWord(WORD_VOR), false); // "vor"
        displayHour = (currentHour % 12) + 1; // Nächste Stunde
        if (displayHour == 13) displayHour = 1;
        hourIndex = displayHour - 1;
        break;
      }
      case 50: {
        setWordWithPriority(ES, getColorForWord(WORD_ES), false); // "ES"
        setWordWithPriority(IST, getColorForWord(WORD_IST), false); // "IST"
        setWordWithPriority(MINUTENADR[1], getColorForWord(WORD_ZEHN_M), false); // "zehn"
        setWordWithPriority(ToPastAddr[0], getColorForWord(WORD_VOR), false); // "vor"
        displayHour = (currentHour % 12) + 1; // Nächste Stunde
        if (displayHour == 13) displayHour = 1;
        hourIndex = displayHour - 1;
        break;
      }
      case 55: {
        setWordWithPriority(ES, getColorForWord(WORD_ES), false); // "ES"
        setWordWithPriority(IST, getColorForWord(WORD_IST), false); // "IST"
        setWordWithPriority(MINUTENADR[0], getColorForWord(WORD_FUENF_M), false); // "fünf"
        setWordWithPriority(ToPastAddr[0], getColorForWord(WORD_VOR), false); // "vor"
        displayHour = (currentHour % 12) + 1; // Nächste Stunde
        if (displayHour == 13) displayHour = 1;
        hourIndex = displayHour - 1;
        break;
      }

      default:
        // Für nicht definierte Basis-Minuten
        break;
    }

    // Setze das Stundenwort entsprechend der Bedingungen für "Ein" und "Eins"
    if (displayHour == 1 && baseMinutes >= 5) {
      setWordWithPriority(EINS, getColorForWord(WORD_EINS), true); // Priorität für "Eins"
      Serial.printf("Stundenwort 'Eins' für Stunde %d gesetzt.\n", displayHour);
    } else {
      int wordIndex = hourWordIndices[hourIndex];
      setWordWithPriority(STUNDENADR[hourIndex], getColorForWord(wordIndex), true); // Priorität für Stundenwort
      Serial.printf("Stundenwort für Stunde %d gesetzt.\n", displayHour);
    }

    // Zusätzliche Minutenanzeige mit '+', '1', '2', '3', '4'
    if (extraMinutes >= 1 && extraMinutes <=4) {
      // '+'-Symbol einschalten
      setSingleLED(LED_PLUS, getColorForWord(WORD_PLUS));
      Serial.println("Zeige '+'-Symbol an.");

      // Alle Zahlen ausschalten
      setSingleLED(LED_1, RgbColor(0, 0, 0));
      setSingleLED(LED_2, RgbColor(0, 0, 0));
      setSingleLED(LED_3, RgbColor(0, 0, 0));
      setSingleLED(LED_4, RgbColor(0, 0, 0));

      if (extraMinutes >= 2) {
        // "Minuten" nur für extraMinutes >=2 setzen
        setWordWithPriority(MINUTEN, getColorForWord(WORD_MINUTEN), true); // "Minuten" setzen mit hoher Priorität
        Serial.println("Setze Wort 'Minuten' mit hoher Priorität.");
      }

      // Für extraMinutes = 1, setze nur "Minute"
      switch(extraMinutes) {
        case 1:
          setSingleLED(LED_1, getColorForWord(WORD_NUM_1));
          setWordWithPriority(MINUTE, getColorForWord(WORD_MINUTE), false); // "Minute" setzen
          Serial.println("Zeige '1' an und setze 'Minute'.");
          break;
        case 2:
          setSingleLED(LED_2, getColorForWord(WORD_NUM_2));
          setWordWithPriority(MINUTE, getColorForWord(WORD_MINUTE), false); // "Minute" setzen
          // "Minuten" wurde bereits gesetzt oben
          Serial.println("Zeige '2' an und setze 'Minute' sowie 'Minuten'.");
          break;
        case 3:
          setSingleLED(LED_3, getColorForWord(WORD_NUM_3));
          setWordWithPriority(MINUTE, getColorForWord(WORD_MINUTE), false); // "Minute" setzen
          // "Minuten" wurde bereits gesetzt oben
          Serial.println("Zeige '3' an und setze 'Minute' sowie 'Minuten'.");
          break;
        case 4:
          setSingleLED(LED_4, getColorForWord(WORD_NUM_4));
          setWordWithPriority(MINUTE, getColorForWord(WORD_MINUTE), false); // "Minute" setzen
          // "Minuten" wurde bereits gesetzt oben
          Serial.println("Zeige '4' an und setze 'Minute' sowie 'Minuten'.");
          break;
      }
    }
    else {
      // '+'-Symbol und Zahlen ausschalten
      setSingleLED(LED_PLUS, RgbColor(0, 0, 0));
      setSingleLED(LED_1, RgbColor(0, 0, 0));
      setSingleLED(LED_2, RgbColor(0, 0, 0));
      setSingleLED(LED_3, RgbColor(0, 0, 0));
      setSingleLED(LED_4, RgbColor(0, 0, 0));

      // "Minute" und "Minuten" ausschalten
      setWordWithPriority(MINUTE, RgbColor(0, 0, 0), false);
      setWordWithPriority(MINUTEN, RgbColor(0, 0, 0), false);

      Serial.println("Zeige '+'-Symbol und Zahlen aus.");
    }

  }
  

  // LEDs aktualisieren
  strip.Show();
  Serial.println("LEDs aktualisiert.");
}

void handleSetFirmwareURL() {
  if (server.hasArg("firmwareurl")) {
    firmwareURL = server.arg("firmwareurl"); // Kein "String" davor, da global bereits deklariert
    preferences.putString("firmwareURL", firmwareURL);
    Serial.println("Firmware-URL aktualisiert auf: " + firmwareURL);
  }
  server.sendHeader("Location", "/update", true);
  server.send(302, "text/plain", "");
}



void handleOnlineUpdate() {
  String html = "<!DOCTYPE html><html><head><title>Online Update</title></head><body>";
  html += "<h1>Online Update durchführen</h1>";
  html += "<p><a href=\"/do_update\">Jetzt Update einspielen</a></p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}



// Handler-Funktion zum Triggern des Updates
void handleDoUpdate() {
  server.send(200, "text/plain", "Update wird gestartet... Bitte warten!");

  // HTTPClient erzeugen und mit der Firmware-URL verbinden
  HTTPClient httpClient;
  httpClient.begin(firmwareURL); // URL zur neuen Firmware

  t_httpUpdate_return ret = httpUpdate.update(httpClient);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("HTTP_UPDATE_FEHLGESCHLAGEN Fehler (%d): %s\n",
                    httpUpdate.getLastError(),
                    httpUpdate.getLastErrorString().c_str());
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("HTTP_UPDATE_KEIN_UPDATE_VERFUEGBAR");
      break;

    case HTTP_UPDATE_OK:
      Serial.println("HTTP_UPDATE_ERFOLGREICH! Neustart...");
      // Gerät startet nach erfolgreichem Update automatisch neu.
      break;
  }

  httpClient.end(); // Verbindung beenden
}


void setup() {
  Serial.begin(115200);
  Serial.println("Starte Word Clock...");
  
  // Zufallsgenerator initialisieren
  randomSeed(analogRead(0));
  
  // Preferences initialisieren
  Serial.println("Initialisiere Preferences...");
  preferences.begin("settings", false);
  Serial.println("Preferences initialisiert.");
  
  // Initialisiere die Hue-Werte für die Wörter
  initializeWordHues();
  
  // Laden der gespeicherten Einstellungen oder setzen von Standardwerten
  brightness = preferences.getUChar("brightness", 50);
  uint32_t colorValue = preferences.getUInt("color", 0xFFFFFF);
  selectedColor = RgbColor((colorValue >> 16) & 0xFF, (colorValue >> 8) & 0xFF, colorValue & 0xFF);

  // Regenbogenmodus standardmäßig aktivieren
  rainbowMode = preferences.getBool("rainbow", true);
  
  // Manuelle Zeit verwenden und standardmäßig auf 12:00 Uhr setzen
  useManualTime = preferences.getBool("useManualTime", true);
  timeOffsetMinutes = preferences.getInt("timeOffset", 0);
  
  // Manuelle Zeit initialisieren, falls nötig
  if (useManualTime) {
    manualTime.tm_year = 2023 - 1900;
    manualTime.tm_mon = 0;
    manualTime.tm_mday = 1;
    manualTime.tm_hour = 12;
    manualTime.tm_min = 0;
    manualTime.tm_sec = 0;

    lastManualUpdateMillis = millis();
    Serial.println("Manuelle Zeit auf 12:00 Uhr initialisiert.");
  }
  
  // LEDs zurücksetzen
  clearLEDs();
  
  // Tagespläne laden
  loadDaySchedules();
  
  // WLAN initialisieren
  WiFi.mode(WIFI_STA);
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");
  if (ssid != "") {
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.print("Verbinde mit WLAN: ");
    Serial.println(ssid);
    int retry = 0;
    const int maxRetries = 20;
    while (WiFi.status() != WL_CONNECTED && retry < maxRetries) {
      delay(500);
      Serial.print(".");
      retry++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nVerbunden mit WLAN!");
      Serial.print("IP-Adresse: ");
      Serial.println(WiFi.localIP());
  
      configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
      Serial.println("Zeitzone konfiguriert.");
    } else {
      Serial.println("\nKonnte nicht mit WLAN verbinden.");
      startAccessPoint(); // Startet den Access Point
    }
  } else {
    startAccessPoint(); // Startet den Access Point für die Konfiguration
  }
  
  // NeoPixel-Strip initialisieren
  strip.Begin();
  strip.Show();
  
  // Webserver starten
  server.on("/", handleRoot);
  server.on("/configure_main", handleConfigureMain);
  server.on("/wifi", handleWifi);
  server.on("/configure_wifi", handleConfigureWifi);
  server.on("/set_manual_time", handleSetManualTime);
  server.on("/toggle_mode", handleToggleMode);
  server.on("/set_time_offset", handleSetTimeOffset);
  server.on("/configure_day_schedules", handleConfigureDaySchedules);
  server.on("/reset", handleReset);
  server.on("/confirm_reset", handleConfirmReset);
  server.on("/update", handleUpdatePage);       // Neue Seite für Update
  server.on("/set_firmware_url", handleSetFirmwareURL);
  server.on("/do_update", handleDoUpdate);

  // Jetzt nur einmal server.begin() aufrufen
  server.begin();
  Serial.println("Webserver gestartet.");
  
  // ... Ihr bisheriger Code vor OTA ...

  // OTA Setup
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "Sketch";
    } else {
      type = "Filesystem";
    }
    Serial.println("OTA Update startet: " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA Update abgeschlossen!");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Fortschritt: %u%%\r", (progress * 100) / total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Fehler [%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Authentifizierungsfehler");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Startfehler");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Verbindungsfehler");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Empfangsfehler");
    else if (error == OTA_END_ERROR) Serial.println("Endfehler");
  });

  ArduinoOTA.begin();
  Serial.println("OTA ist bereit!");
  
  firmwareURL = preferences.getString("firmwareURL", "http://example.com/firmware.bin");
  
  
  // Jetzt den Webserver starten (nur einmal)
  
  Serial.println("Webserver gestartet. Aufrufen: http://<IP>/update");

  initialized = true;
}


void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  
  static unsigned long lastDisplayTime = 0;
  unsigned long currentMillis = millis();

  if (currentMillis - lastDisplayTime >= 1000) {
    lastDisplayTime = currentMillis;
    displayTime();
  }

  static unsigned long lastConnectionCheck = 0;
  const unsigned long connectionCheckInterval = 60000;

  if (currentMillis - lastConnectionCheck >= connectionCheckInterval) {
    lastConnectionCheck = currentMillis;
    if (WiFi.status() == WL_CONNECTED && useManualTime) {
      struct tm timeInfo;
      if (getLocalTime(&timeInfo)) {
        useManualTime = false;
        preferences.putBool("useManualTime", useManualTime);
        Serial.println("Internetverbindung erkannt. Wechsel zu Online-Modus.");

        manualTime = timeInfo;
        lastManualUpdateMillis = millis();
        Serial.println("Zeit synchronisiert nach Wechsel zu Online-Modus.");
      }
    }
  }
}
