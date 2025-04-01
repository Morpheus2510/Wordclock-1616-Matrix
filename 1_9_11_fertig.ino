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
#include <lwip/dns.h>
#include <ESPmDNS.h>  // Für mDNS-Unterstützung
#include <Wire.h>
#include <RTClib.h>  // Adafruit RTClib für DS3231

// ========================================================
//   LED-Konfiguration
// ========================================================
#define NUM_LEDS 256  // Gesamtzahl der LEDs in der Matrix
#define DATA_PIN 32   // Pin für NeoPixel-Signal

// Matrix-Größe
#define MATRIX_WIDTH 16
#define MATRIX_HEIGHT 16

// Anzahl der aktiven Reihen mit LEDs
#define NUM_ACTIVE_ROWS 8

// NeoPixelBrightnessBus-Objekt (ESP32 RMT, 800 Kbps)
NeoPixelBrightnessBus<NeoGrbFeature, NeoEsp32Rmt0800KbpsMethod> strip(NUM_LEDS, DATA_PIN);

// Webserver auf Port 80
WebServer server(80);

// ========================================================
//   Globale Variablen
// ========================================================
uint8_t brightness = 50;                // Standardhelligkeit (0-255)
uint8_t dimBrightness = 25;            // Standard-Dimm-Helligkeit (0-100)
RgbColor selectedColor(255, 255, 255);  // Standardfarbe: Weiß
bool rainbowMode = true;                // Regenbogenmodus (an/aus) – standardmäßig aktiviert

// DS3231-RTC
RTC_DS3231 rtc;
bool rtcAvailable = false;  // Wird auf true gesetzt, wenn DS3231 erfolgreich erkannt wird

// Firmware-Update
// HIER haben wir den Default-Link auf dein GitHub-Release gesetzt:
String firmwareURL = "https://github.com/Morpheus2510/Wordclock-1616-Matrix/releases/download/v1_9_9/WordClock_v1_9_10.ino.bin";

// Statische IP-Konfiguration
bool useDHCP = true;
String staticIP = "192.168.4.50";
String staticGateway = "192.168.4.1";
String staticSubnet = "255.255.255.0";

// Offset in Minuten (z. B. +2 oder -5 etc.)
int timeOffsetMinutes = 0;

// Preferences (EEPROM-Ersatz)
Preferences preferences;

// Kennzeichnet, ob die Uhr jemals erfolgreich gestellt wurde
bool initialTimeSet = false;

// Flag, ob Setup fertig ist
bool initialized = false;

// NEU: Flag und Zeit fürs 30-Sekunden-WiFi-Indicator-Feature
bool showWifiIndicator = true;
unsigned long wifiIndicatorStartTime = 0;

// Globale Variablen
bool isDST = false;  // Sommerzeit aktiv (true) oder Winterzeit (false)

// ========================================================
//   Variablen für Software-Uhr
// ========================================================
static bool isTimeValid = false;  // Kennzeichnet, ob wir zumindest einmal eine gültige Uhrzeit hatten
static unsigned long lastMillisForSoftwareClock = 0;
static time_t softwareTime = 0;  // Wird in Sekunden gehalten (UNIX-Timestamp)


// ========================================================
//   Datenstruktur für Tagespläne (Dimmen / Abschalten)
// ========================================================
struct DaySchedule {
  int startHour;
  int startMinute;
  int endHour;
  int endMinute;
  bool dimmed;  // true => LED gedimmt, false => LED aus
};

// Array aller Wochentage (0 = Sonntag, 1 = Montag, ...)
DaySchedule weekSchedules[7];

// Buffer für LED-Zustand, damit wir nicht versehentlich Wort-LEDs überschreiben
RgbColor ledState[NUM_LEDS];

// ========================================================
//   LED-Wort-Indizes Struktur (unverändert)
// ========================================================
struct WordIndices {
  int start;
  int end;
};

WordIndices STUNDENADR[12] = {
  { 91, 89 },    // "Ein"
  { 127, 124 },  // "Zwei"
  { 123, 120 },  // "Drei"
  { 115, 112 },  // "Vier"
  { 83, 80 },    // "Fünf"
  { 88, 84 },    // "Sechs"
  { 63, 58 },    // "Sieben"
  { 95, 92 },    // "Acht"
  { 119, 116 },  // "Neun"
  { 150, 147 },  // "Zehn"
  { 146, 144 },  // "Elf"
  { 57, 53 }     // "Zwölf"
};

WordIndices ES = { 255, 254 };
WordIndices IST = { 252, 250 };

WordIndices ToPastAddr[2] = {
  { 159, 157 },  // Vor
  { 179, 176 }   // Nach
};

WordIndices HalfFullHourAddr[2] = {
  { 155, 152 },  // Halb
  { 50, 48 }     // Uhr
};

WordIndices MINUTE = { 23, 18 };
WordIndices MINUTEN = { 23, 17 };

WordIndices MINUTENADR[4] = {
  { 191, 188 },  // Fünf
  { 211, 208 },  // Zehn
  { 187, 181 },  // Viertel
  { 223, 217 }   // Zwanzig
};

const int LED_PLUS = 1;
const int LED_1 = 3;
const int LED_2 = 4;
const int LED_3 = 5;
const int LED_4 = 6;

WordIndices EINS = { 91, 88 };

// LED-Zuordnung pro aktiver Reihe
const int rowLeds[NUM_ACTIVE_ROWS][16] = {
  { 255, 254, 253, 252, 251, 250, 249, 248, 247, 246, 245, 244, 243, 242, 241, 240 },
  { 223, 222, 221, 220, 219, 218, 217, 216, 215, 214, 213, 212, 211, 210, 209, 208 },
  { 191, 190, 189, 188, 187, 186, 185, 184, 183, 182, 181, 180, 179, 178, 177, 176 },
  { 159, 158, 157, 156, 155, 154, 153, 152, 151, 150, 149, 148, 147, 146, 145, 144 },
  { 127, 126, 125, 124, 123, 122, 121, 120, 119, 118, 117, 116, 115, 114, 113, 112 },
  { 95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80 },
  { 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48 },
  { 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16 }
};

// ========================================================
//   Enum für Wortindizes (Regenbogen-Funktion)
// ========================================================
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
  WORD_MINUTE,
  WORD_MINUTEN,
  WORD_FUENF_M,
  WORD_ZEHN_M,
  WORD_VIERTEL,
  WORD_ZWANZIG,
  WORD_PLUS,
  WORD_NUM_1,
  WORD_NUM_2,
  WORD_NUM_3,
  WORD_NUM_4,
  NUM_WORDS
};

uint16_t wordHues[NUM_WORDS];  // Hue-Werte pro Wort (für Regenbogen)

// ========================================================
//   Hilfsfunktionen (HSV → RGB, Regenbogen, etc.)
// ========================================================
void initializeWordHues() {
  for (int i = 0; i < NUM_WORDS; i++) {
    wordHues[i] = random(0, 360);
    Serial.printf("Initial Hue für Wort %d: %d\n", i, wordHues[i]);
  }
}

RgbColor hsvToRgb(uint16_t hue, uint8_t saturation, uint8_t value) {
  uint8_t region = hue / 60;
  uint8_t remainder = hue % 60;

  uint8_t p = (value * (255 - saturation)) / 255;
  uint8_t q = (value * (255 - ((saturation * remainder) / 60))) / 255;
  uint8_t t = (value * (255 - ((saturation * (60 - remainder)) / 60))) / 255;

  switch (region) {
    case 0: return RgbColor(value, t, p);
    case 1: return RgbColor(q, value, p);
    case 2: return RgbColor(p, value, t);
    case 3: return RgbColor(p, q, value);
    case 4: return RgbColor(t, p, value);
    default: return RgbColor(value, p, q);
  }
}

RgbColor getColorForWord(WordIndex word) {
  if (rainbowMode) {
    if (word == WORD_MINUTEN) {
      // "Minuten" nimmt Hue von "Minute"
      return hsvToRgb(wordHues[WORD_MINUTE], 255, 255);
    } else {
      return hsvToRgb(wordHues[word], 255, 255);
    }
  } else {
    return selectedColor;
  }
}

// ========================================================
//   LED-Set-Funktionen
// ========================================================
void clearLEDs() {
  for (int i = 0; i < NUM_LEDS; i++) {
    ledState[i] = RgbColor(0, 0, 0);
    strip.SetPixelColor(i, ledState[i]);
  }
}

void setWordWithPriority(WordIndices word, RgbColor color, bool highPriority = false) {
  if (word.start <= word.end) {
    for (int i = word.start; i <= word.end; i++) {
      if (i >= 0 && i < NUM_LEDS) {
        if (highPriority || (ledState[i].R == 0 && ledState[i].G == 0 && ledState[i].B == 0)) {
          ledState[i] = color;
          strip.SetPixelColor(i, color);
        }
      }
    }
  } else {
    for (int i = word.start; i >= word.end; i--) {
      if (i >= 0 && i < NUM_LEDS) {
        if (highPriority || (ledState[i].R == 0 && ledState[i].G == 0 && ledState[i].B == 0)) {
          ledState[i] = color;
          strip.SetPixelColor(i, color);
        }
      }
    }
  }
}

void setSingleLED(int index, RgbColor color) {
  if (index >= 0 && index < NUM_LEDS) {
    ledState[index] = color;
    strip.SetPixelColor(index, color);
  }
}

// ========================================================
//   Webserver: Handler-Funktionen
// ========================================================

// --------------------------------------------------------
//  Startseite / Root
// --------------------------------------------------------
void handleRoot() {
  Serial.println("Anfrage an handleRoot empfangen.");
  String html = "<!DOCTYPE html><html><head><title>Word Clock</title>";
  html += "<meta charset=\"UTF-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<style>";
  html += "body { font-family: 'Segoe UI', Arial, sans-serif; margin: 0; padding: 20px; background-color: #f5f5f5; }";
  html += "h1, h2 { color: #333; text-align: center; margin-bottom: 30px; }";
  html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
  html += ".menu-item { display: block; padding: 15px; margin: 10px 0; background: #f8f9fa; border-radius: 5px; text-decoration: none; color: #333; transition: all 0.3s ease; text-align: center; font-weight: bold; }";
  html += ".menu-item:hover { background: #e9ecef; transform: translateX(5px); box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
  html += ".status-info { background: #e9ecef; padding: 15px; border-radius: 5px; margin: 10px 0; }";
  html += "form { margin: 20px 0; }";
  html += "input[type='color'], input[type='number'], input[type='checkbox'] { margin: 10px 0; }";
  html += "input[type='submit'] { background: #007bff; color: white; border: none; padding: 12px 20px; border-radius: 5px; cursor: pointer; width: 100%; font-weight: bold; }";
  html += "input[type='submit']:hover { background: #0056b3; box-shadow: 0 2px 5px rgba(0,0,0,0.2); }";
  html += ".warning { color: #dc3545; font-weight: bold; }";
  html += ".settings-section { background: #f8f9fa; padding: 20px; border-radius: 5px; margin: 20px 0; }";
  html += "label { display: block; margin-bottom: 5px; font-weight: bold; }";
  html += ".back-button { display: inline-block; padding: 12px 20px; background: #6c757d; color: white; text-decoration: none; border-radius: 5px; margin-top: 20px; transition: all 0.3s ease; }";
  html += ".back-button:hover { background: #5a6268; transform: translateX(-5px); box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
  html += ".nav-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin: 20px 0; }";
  html += ".nav-button { display: block; padding: 20px; background: #007bff; color: white; text-decoration: none; border-radius: 5px; text-align: center; font-weight: bold; transition: all 0.3s ease; }";
  html += ".nav-button:hover { background: #0056b3; transform: translateY(-3px); box-shadow: 0 4px 8px rgba(0,0,0,0.2); }";
  html += ".nav-button i { display: block; font-size: 24px; margin-bottom: 10px; }";
  html += "</style>";
  html += "</head><body>";
  html += "<div class=\"container\">";
  html += "<h1>Word Clock Konfiguration</h1>";

  // Gerätestatus
  html += "<div class=\"status-info\"><strong>Ersteller:</strong> Morpheus2510</div>";

  // Reset-Link
  html += "<a href=\"/reset\" class=\"menu-item warning\" onclick=\"return confirm('Möchten Sie das Gerät wirklich zurücksetzen? Alle Einstellungen gehen verloren.');\">Gerät zurücksetzen</a>";

  // Aktuelle Uhrzeit anzeigen (NTP vs. DS3231)
  struct tm timeInfo;
  bool timeOk = false;

  // Erst versuchen wir NTP (falls WLAN)
  if (WiFi.status() == WL_CONNECTED) {
    if (getLocalTime(&timeInfo)) {
      timeInfo.tm_min += timeOffsetMinutes;
      mktime(&timeInfo);
      timeOk = true;
      char timeStr[16];
      sprintf(timeStr, "%02d:%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
      html += "<div class=\"status-info\">Aktuelle Uhrzeit (Online/NTP): " + String(timeStr) + "</div>";
    } else {
      html += "<div class=\"status-info\">Aktuelle Uhrzeit: NTP nicht verfügbar</div>";
    }
  }

  // Dann DS3231
  if (!timeOk) {
    if (rtcAvailable) {
      DateTime now = rtc.now();
      char dsStr[16];
      sprintf(dsStr, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
      html += "<div class=\"status-info\">Aktuelle Uhrzeit (DS3231): " + String(dsStr) + "</div>";
      timeOk = true;
    }
  }

  // Falls beides nicht geht, nutzen wir die Software-Uhr, falls verfügbar
  if (!timeOk) {
    if (isTimeValid) {
      time_t tmp = softwareTime;
      tmp += (timeOffsetMinutes * 60);
      struct tm* softTm = localtime(&tmp);
      char swStr[16];
      sprintf(swStr, "%02d:%02d:%02d", softTm->tm_hour, softTm->tm_min, softTm->tm_sec);
      html += "<div class=\"status-info\">Aktuelle Uhrzeit (Software-Uhr): " + String(swStr) + "</div>";
    } else {
      html += "<div class=\"status-info\">Keine RTC erkannt, kein WLAN oder Zeit noch nie gesetzt.</div>";
    }
  }

  // IP anzeigen
  html += "<div class=\"status-info\">Lokale IP-Adresse: " + WiFi.localIP().toString() + "</div>";

  // Navigation als Grid-Buttons
  html += "<div class=\"settings-section\">";
  html += "<h2>Navigation</h2>";
  html += "<div class=\"nav-grid\">";
  html += "<a href=\"/diagnose\" class=\"nav-button\"><i>🔍</i>Diagnose</a>";
  html += "<a href=\"/update\" class=\"nav-button\"><i>🔄</i>Upgrade</a>";
  html += "<a href=\"/wifi\" class=\"nav-button\"><i>📡</i>Wi-Fi</a>";
  html += "<a href=\"/set_time_offset\" class=\"nav-button\"><i>⏰</i>Zeit-Offset</a>";
  html += "<a href=\"/dst\" class=\"nav-button\"><i>🌞</i>Sommerzeit</a>";
  html += "<a href=\"/configure_day_schedules\" class=\"nav-button\"><i>📅</i>Tagespläne</a>";
  html += "<a href=\"/timezone\" class=\"nav-button\"><i>🌍</i>Zeitzone</a>";
  html += "</div>";
  html += "</div>";

  // Farbe, Helligkeit, Regenbogenmodus
  html += "<div class=\"settings-section\">";
  html += "<h2>Anzeige-Einstellungen</h2>";
  html += "<form action=\"/configure_main\" method=\"post\">";
  html += "<div class=\"status-info\">";
  html += "<label>Farbe:</label><br>";
  html += "<input type=\"color\" name=\"color\" value=\"#";
  char colorStrFinal[7];
  sprintf(colorStrFinal, "%02X%02X%02X", selectedColor.R, selectedColor.G, selectedColor.B);
  html += String(colorStrFinal) + "\"><br>";
  html += "<label>Helligkeit (0-255):</label><br>";
  html += "<input type=\"number\" name=\"brightness\" min=\"0\" max=\"150\" value=\"" + String(brightness) + "\"><br>";
  html += "<label>Dimm-Helligkeit (0-100):</label><br>";
  html += "<input type=\"number\" name=\"dimBrightness\" min=\"0\" max=\"100\" value=\"" + String(dimBrightness) + "\"><br>";
  html += "<label>Regenbogenmodus:</label><br>";
  html += "<input type=\"checkbox\" name=\"rainbow\" " + String(rainbowMode ? "checked" : "") + ">";
  html += "</div>";
  html += "<input type=\"submit\" value=\"Speichern\">";
  html += "</form>";
  html += "</div>";

  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

// --------------------------------------------------------
//  Diagnose
// --------------------------------------------------------
void handleDiagnose() {
  Serial.println("Anfrage an handleDiagnose empfangen.");

  String html = "<!DOCTYPE html><html><head><title>Diagnose</title>";
  html += "<meta charset=\"UTF-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<style>";
  html += "body { font-family: 'Segoe UI', Arial, sans-serif; margin: 0; padding: 20px; background-color: #f5f5f5; }";
  html += "h1 { color: #333; text-align: center; margin-bottom: 30px; }";
  html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
  html += "table { border-collapse: collapse; width: 100%; margin: 20px 0; }";
  html += "th, td { padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }";
  html += "th { background-color: #f8f9fa; font-weight: bold; }";
  html += "tr:hover { background-color: #f5f5f5; }";
  html += ".status-ok { color: #28a745; }";
  html += ".status-warning { color: #ffc107; }";
  html += ".status-error { color: #dc3545; }";
  html += ".back-button { display: inline-block; padding: 12px 20px; background: #6c757d; color: white; text-decoration: none; border-radius: 5px; margin-top: 20px; transition: all 0.3s ease; }";
  html += ".back-button:hover { background: #5a6268; transform: translateX(-5px); box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
  html += ".section-title { color: #007bff; margin-top: 30px; margin-bottom: 15px; }";
  html += "</style></head><body>";
  html += "<div class=\"container\">";
  html += "<h1>Diagnose-Seite</h1>";

  // Beispiel-Firmware-Version
  String firmwareVersion = "1.9.11";

  // Aktuelle Uhrzeit und Datum
  struct tm timeInfo;
  bool timeValid = getLocalTime(&timeInfo);
  char timeStr[64];
  if (timeValid) {
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeInfo);
  } else {
    snprintf(timeStr, sizeof(timeStr), "Nicht verfügbar");
  }

  IPAddress ip = WiFi.localIP();
  IPAddress gw = WiFi.gatewayIP();
  IPAddress sn = WiFi.subnetMask();

  const ip_addr_t* pDnsAddr0 = dns_getserver(0);
  const ip_addr_t* pDnsAddr1 = dns_getserver(1);
  const ip_addr_t* pDnsAddr2 = dns_getserver(2);

  IPAddress dns0 = IPAddress(ntohl(pDnsAddr0->u_addr.ip4.addr));
  IPAddress dns1_ = IPAddress(ntohl(pDnsAddr1->u_addr.ip4.addr));
  IPAddress dns2_ = IPAddress(ntohl(pDnsAddr2->u_addr.ip4.addr));

  wl_status_t wifiStatus = WiFi.status();
  String wifiStatusStr = "Unbekannt";
  String wifiStatusClass = "status-warning";
  if (wifiStatus == WL_CONNECTED) {
    wifiStatusStr = "Verbunden";
    wifiStatusClass = "status-ok";
  }
  else if (wifiStatus == WL_DISCONNECTED) {
    wifiStatusStr = "Getrennt";
    wifiStatusClass = "status-error";
  }
  else if (wifiStatus == WL_NO_SSID_AVAIL) {
    wifiStatusStr = "Keine SSID gefunden";
    wifiStatusClass = "status-error";
  }
  else if (wifiStatus == WL_CONNECT_FAILED) {
    wifiStatusStr = "Verbindung fehlgeschlagen";
    wifiStatusClass = "status-error";
  }
  else if (wifiStatus == WL_IDLE_STATUS) {
    wifiStatusStr = "Idle";
    wifiStatusClass = "status-warning";
  }

  String ssid = WiFi.SSID();
  int32_t rssi = WiFi.RSSI();

  unsigned long uptimeMillis = millis();
  unsigned long uptimeSec = uptimeMillis / 1000;
  unsigned long uptimeHours = uptimeSec / 3600;
  unsigned long uptimeMins = (uptimeSec % 3600) / 60;
  unsigned long uptimeS = uptimeSec % 60;

  String timeZoneInfo = "CET-1CEST,M3.5.0,M10.5.0/3";

  html += "<h2 class=\"section-title\">Systeminformationen</h2>";
  html += "<table>";
  html += "<tr><th>Beschreibung</th><th>Wert</th></tr>";
  html += "<tr><td>Firmware-Version</td><td>" + firmwareVersion + "</td></tr>";
  html += "<tr><td>Aktuelle Uhrzeit</td><td>" + String(timeStr) + "</td></tr>";
  html += "<tr><td>Zeitzone</td><td>" + timeZoneInfo + "</td></tr>";
  html += "<tr><td>Uptime</td><td>" + String(uptimeHours) + "h " + String(uptimeMins) + "m " + String(uptimeS) + "s</td></tr>";
  html += "<tr><td>Verbindungsstatus WLAN</td><td class=\"" + wifiStatusClass + "\">" + wifiStatusStr + "</td></tr>";
  if (wifiStatus == WL_CONNECTED) {
    html += "<tr><td>SSID</td><td>" + ssid + "</td></tr>";
    html += "<tr><td>Signalstärke (RSSI)</td><td>" + String(rssi) + " dBm</td></tr>";
  }
  html += "</table>";

  html += "<h2 class=\"section-title\">Netzwerkeinstellungen</h2>";
  html += "<table>";
  html += "<tr><th>Beschreibung</th><th>Wert</th></tr>";
  html += "<tr><td>IP-Adresse</td><td>" + ip.toString() + "</td></tr>";
  html += "<tr><td>Gateway</td><td>" + gw.toString() + "</td></tr>";
  html += "<tr><td>Subnet</td><td>" + sn.toString() + "</td></tr>";
  html += "<tr><td>DNS-Server 1</td><td>" + dns0.toString() + "</td></tr>";
  html += "<tr><td>DNS-Server 2</td><td>" + dns1_.toString() + "</td></tr>";
  html += "<tr><td>DNS-Server 3</td><td>" + dns2_.toString() + "</td></tr>";
  html += "</table>";

  if (rtcAvailable) {
    html += "<h2 class=\"section-title\">Hardware-Status</h2>";
    html += "<table>";
    html += "<tr><th>Beschreibung</th><th>Wert</th></tr>";
    float ds3231Temperature = rtc.getTemperature();
    html += "<tr><td>DS3231 Temperatur</td><td>" + String(ds3231Temperature, 2) + " °C</td></tr>";
    html += "</table>";
  }

  html += "<a href=\"/\" class=\"back-button\">Zurück zur Startseite</a>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

// --------------------------------------------------------
//  Handler für Konfiguration (Farbe, Helligkeit, Rainbow)
// --------------------------------------------------------
void handleConfigureMain() {
  Serial.println("Anfrage an handleConfigureMain empfangen.");

  // Farbe
  if (server.hasArg("color")) {
    String color = server.arg("color");
    long number = strtol(&color[1], NULL, 16);
    selectedColor = RgbColor((number >> 16) & 0xFF, (number >> 8) & 0xFF, number & 0xFF);
    uint32_t colorValue = ((uint32_t)selectedColor.R << 16) | ((uint32_t)selectedColor.G << 8) | selectedColor.B;
    preferences.putUInt("color", colorValue);
    Serial.println("Farbe aktualisiert und gespeichert.");
  }

  // Helligkeit
  if (server.hasArg("brightness")) {
    brightness = server.arg("brightness").toInt();
    // Skaliere die Helligkeit so, dass 150 als 100% interpretiert wird
    uint8_t scaledBrightness = map(brightness, 0, 150, 0, 255);  // Skaliere auf 0-255
    strip.SetBrightness(scaledBrightness);
    preferences.putUChar("brightness", brightness);
    Serial.println("Helligkeit aktualisiert und gespeichert.");
  }

  // Dimm-Helligkeit
  if (server.hasArg("dimBrightness")) {
    dimBrightness = server.arg("dimBrightness").toInt();
    preferences.putUChar("dimBrightness", dimBrightness);
    Serial.println("Dimm-Helligkeit aktualisiert und gespeichert.");
  }

  // Regenbogen
  bool newRainbowMode = server.hasArg("rainbow");
  if (rainbowMode != newRainbowMode) {
    rainbowMode = newRainbowMode;
    preferences.putBool("rainbow", rainbowMode);
    Serial.println("Regenbogenmodus aktualisiert und gespeichert.");
  }

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// --------------------------------------------------------
//  Reset
// --------------------------------------------------------
void handleReset() {
  Serial.println("Anfrage an handleReset empfangen.");
  String html = "<!DOCTYPE html><html><head><title>Gerät zurücksetzen</title>";
  html += "<meta charset=\"UTF-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<style>";
  html += "body { font-family: 'Segoe UI', Arial, sans-serif; margin: 0; padding: 20px; background-color: #f5f5f5; }";
  html += "h1 { color: #333; text-align: center; margin-bottom: 30px; }";
  html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
  html += ".warning-box { background: #f8d7da; color: #721c24; padding: 20px; border-radius: 5px; margin: 20px 0; border: 1px solid #f5c6cb; }";
  html += ".warning-icon { font-size: 24px; margin-right: 10px; }";
  html += "form { margin: 20px 0; }";
  html += "input[type='submit'] { background: #dc3545; color: white; border: none; padding: 12px 20px; border-radius: 5px; cursor: pointer; width: 100%; font-weight: bold; }";
  html += "input[type='submit']:hover { background: #c82333; }";
  html += ".back-button { display: inline-block; padding: 12px 20px; background: #6c757d; color: white; text-decoration: none; border-radius: 5px; margin-top: 20px; transition: all 0.3s ease; }";
  html += ".back-button:hover { background: #5a6268; transform: translateX(-5px); box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
  html += ".info-box { background: #e9ecef; padding: 15px; border-radius: 5px; margin: 20px 0; }";
  html += "</style>";
  html += "</head><body>";
  html += "<div class=\"container\">";
  html += "<h1>Gerät zurücksetzen</h1>";
  html += "<div class=\"warning-box\">";
  html += "<span class=\"warning-icon\">⚠️</span>";
  html += "<strong>Achtung!</strong> Diese Aktion setzt alle Einstellungen zurück, einschließlich:";
  html += "<ul>";
  html += "<li>WLAN-Konfiguration</li>";
  html += "<li>Zeitzone und Zeit-Offset</li>";
  html += "<li>LED-Farbe und Helligkeit</li>";
  html += "<li>Tagespläne</li>";
  html += "</ul>";
  html += "Das Gerät wird nach dem Zurücksetzen neu gestartet.";
  html += "</div>";
  html += "<div class=\"info-box\">";
  html += "Nach dem Zurücksetzen können Sie das Gerät über den Access Point 'WordClock' mit dem Passwort 'password123' erreichen.";
  html += "</div>";
  html += "<form action=\"/confirm_reset\" method=\"post\" onsubmit=\"return confirm('Sind Sie sicher, dass Sie das Gerät zurücksetzen möchten? Diese Aktion kann nicht rückgängig gemacht werden!');\">";
  html += "<input type=\"submit\" value=\"Ja, Gerät zurücksetzen\">";
  html += "</form>";
  html += "<a href=\"/\" class=\"back-button\">Abbrechen und zurück zur Startseite</a>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleConfirmReset() {
  Serial.println("Gerät wird zurückgesetzt...");
  WiFi.disconnect(true, true);
  preferences.clear();
  server.send(200, "text/plain", "Das Gerät wird zurückgesetzt und neu gestartet...");
  delay(2000);
  ESP.restart();
}

// --------------------------------------------------------
//  Wi-Fi-Konfigurationsseite
// --------------------------------------------------------
void handleWifi() {
  Serial.println("Anfrage an handleWifi empfangen.");
  String html = "<!DOCTYPE html><html><head><title>Wi-Fi Einstellungen</title>";
  html += "<meta charset=\"UTF-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<style>";
  html += "body { font-family: 'Segoe UI', Arial, sans-serif; margin: 0; padding: 20px; background-color: #f5f5f5; }";
  html += "h1, h2 { color: #333; text-align: center; margin-bottom: 30px; }";
  html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
  html += ".form-group { margin-bottom: 20px; }";
  html += "label { display: block; margin-bottom: 5px; font-weight: bold; }";
  html += "input[type='text'], input[type='password'], select { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 5px; margin-bottom: 10px; }";
  html += "input[type='submit'] { background: #007bff; color: white; border: none; padding: 12px 20px; border-radius: 5px; cursor: pointer; width: 100%; }";
  html += "input[type='submit']:hover { background: #0056b3; }";
  html += ".back-button { display: inline-block; padding: 12px 20px; background: #6c757d; color: white; text-decoration: none; border-radius: 5px; margin-top: 20px; transition: all 0.3s ease; }";
  html += ".back-button:hover { background: #5a6268; transform: translateX(-5px); box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
  html += ".success-message { background: #d4edda; color: #155724; padding: 15px; border-radius: 5px; margin: 20px 0; }";
  html += ".error-message { background: #f8d7da; color: #721c24; padding: 15px; border-radius: 5px; margin: 20px 0; }";
  html += ".section { background: #f8f9fa; padding: 20px; border-radius: 5px; margin: 20px 0; }";
  html += ".radio-group { margin: 10px 0; }";
  html += ".radio-group label { display: inline; font-weight: normal; }";
  html += ".help-text { color: #666; font-size: 0.9em; margin-top: 5px; }";
  html += "</style>";
  html += "</head><body>";
  html += "<div class=\"container\">";
  html += "<h1>Wi-Fi Konfiguration</h1>";

  if (server.hasArg("status")) {
    String status = server.arg("status");
    if (status == "success") {
      html += "<div class=\"success-message\">✓ Verbindung zum WLAN erfolgreich hergestellt!</div>";
    } else if (status == "failed") {
      html += "<div class=\"error-message\">✗ Verbindung zum WLAN fehlgeschlagen. Bitte überprüfen Sie die Zugangsdaten und versuchen Sie es erneut.</div>";
    }
  }

  html += "<div class=\"section\">";
  html += "<h2>WLAN-Verbindung</h2>";
  html += "<form action=\"/configure_wifi\" method=\"post\">";

  int n = WiFi.scanNetworks();
  if (n == 0) {
    html += "<div class=\"error-message\">Keine WLAN-Netzwerke gefunden.</div>";
  } else {
    html += "<div class=\"form-group\">";
    html += "<label for=\"ssid\">Verfügbare WLAN-Netzwerke:</label>";
    html += "<select name=\"ssid\" id=\"ssid\">";
    for (int i = 0; i < n; ++i) {
      String ssidItem = WiFi.SSID(i);
      ssidItem.replace("\"", "&quot;");
      html += "<option value=\"" + ssidItem + "\">" + ssidItem + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
    }
    html += "</select>";
    html += "</div>";
  }

  html += "<div class=\"form-group\">";
  html += "<label for=\"password\">WLAN Passwort:</label>";
  html += "<input type=\"password\" id=\"password\" name=\"password\">";
  html += "</div>";
  html += "<input type=\"submit\" value=\"Speichern\">";
  html += "</form>";
  html += "</div>";

  html += "<div class=\"section\">";
  html += "<h2>IP-Konfiguration</h2>";
  html += "<form action=\"/configure_ip\" method=\"post\">";
  html += "<div class=\"form-group\">";
  html += "<label>Modus:</label>";
  html += "<div class=\"radio-group\">";
  html += "<input type=\"radio\" name=\"mode\" value=\"dhcp\" " + String(useDHCP ? "checked" : "") + " id=\"dhcp\">";
  html += "<label for=\"dhcp\">DHCP (automatisch)</label>";
  html += "</div>";
  html += "<div class=\"radio-group\">";
  html += "<input type=\"radio\" name=\"mode\" value=\"static\" " + String(!useDHCP ? "checked" : "") + " id=\"static\">";
  html += "<label for=\"static\">Statisch</label>";
  html += "</div>";
  html += "</div>";

  html += "<div class=\"form-group\">";
  html += "<label>IP-Adresse (nur bei statisch):</label>";
  html += "<input type=\"text\" name=\"ip\" value=\"" + staticIP + "\">";
  html += "</div>";

  html += "<div class=\"form-group\">";
  html += "<label>Gateway (nur bei statisch):</label>";
  html += "<input type=\"text\" name=\"gateway\" value=\"" + staticGateway + "\">";
  html += "</div>";

  html += "<div class=\"form-group\">";
  html += "<label>Subnet (nur bei statisch):</label>";
  html += "<input type=\"text\" name=\"subnet\" value=\"" + staticSubnet + "\">";
  html += "</div>";

  html += "<input type=\"submit\" value=\"IP-Einstellungen speichern\">";
  html += "</form>";
  html += "</div>";

  html += "<a href=\"/\" class=\"back-button\">Zurück zur Startseite</a>";
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

// --------------------------------------------------------
//  Setzen von WLAN-Konfiguration
// --------------------------------------------------------
void handleConfigureWifi() {
  Serial.println("Anfrage an handleConfigureWifi empfangen.");
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  preferences.putString("ssid", ssid);
  preferences.putString("password", password);

  // Mit neuem WLAN verbinden
  WiFi.begin(ssid.c_str(), password.c_str());
  int retry = 0;
  const int maxRetries = 40;
  bool connected = false;
  while (WiFi.status() != WL_CONNECTED && retry < maxRetries) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nVerbunden mit WLAN!");
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");

    struct tm timeInfo;
    int retryTime = 0;
    while (!getLocalTime(&timeInfo) && retryTime < 10) {
      Serial.println("Warte auf Zeitsynchronisation...");
      delay(1000);
      retryTime++;
    }
    if (getLocalTime(&timeInfo)) {
      Serial.println("Zeit erfolgreich synchronisiert (NTP).");
      connected = true;

      // DS3231 aktualisieren
      if (rtcAvailable) {
        rtc.adjust(DateTime(
          timeInfo.tm_year + 1900,
          timeInfo.tm_mon + 1,
          timeInfo.tm_mday,
          timeInfo.tm_hour,
          timeInfo.tm_min,
          timeInfo.tm_sec));
        Serial.println("DS3231 mit NTP-Zeit aktualisiert (Neue WLAN-Einstellungen).");
      }

      // NEU: Software-Uhr füllen
      softwareTime = mktime(&timeInfo);
      lastMillisForSoftwareClock = millis();
      isTimeValid = true;

      if (!initialTimeSet) {
        initialTimeSet = true;
        preferences.putBool("initialTimeSet", initialTimeSet);
      }
    } else {
      Serial.println("NTP nicht verfügbar. Zeit konnte nicht synchronisiert werden.");
    }
  } else {
    Serial.println("\nVerbindung mit WLAN fehlgeschlagen.");
  }

  String status = connected ? "success" : "failed";
  server.sendHeader("Location", "/wifi?status=" + status, true);
  server.send(302, "text/plain", "");
}

// --------------------------------------------------------
//  Zeit-Offset einstellen
// --------------------------------------------------------
void handleSetTimeOffset() {
  Serial.println("Anfrage an handleSetTimeOffset empfangen.");
  if (server.method() == HTTP_GET) {
    String html = "<!DOCTYPE html><html><head><title>Zeit-Offset einstellen</title>";
    html += "<meta charset=\"UTF-8\">";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<style>body { font-family: Arial, sans-serif; margin: 20px; }</style>";
    html += "</head><body>";
    html += "<h1>Zeit-Offset einstellen</h1>";
    html += "<form action=\"/set_time_offset\" method=\"post\">";
    html += "Zeit-Offset in Minuten (z.B. -5, +10):<br>";
    html += "<input type=\"number\" name=\"offset\" min=\"-60\" max=\"60\" value=\"" + String(timeOffsetMinutes) + "\">";
    html += "<br><br><input type=\"submit\" value=\"Speichern\"></form>";
    html += "<p><a href=\"/\">Zurück zur Startseite</a></p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  } else if (server.method() == HTTP_POST) {
    if (server.hasArg("offset")) {
      timeOffsetMinutes = server.arg("offset").toInt();
      preferences.putInt("timeOffset", timeOffsetMinutes);
      Serial.printf("Zeit-Offset gesetzt: %d Minuten\n", timeOffsetMinutes);
      server.sendHeader("Location", "/", true);
      server.send(302, "text/plain", "");
    } else {
      server.send(400, "text/plain", "Ungültige Anfrage");
    }
  }
}

// --------------------------------------------------------
//  Tagespläne (Dimmed / Nachtmodus)
// --------------------------------------------------------
void handleConfigureDaySchedules() {
  Serial.println("Anfrage an handleConfigureDaySchedules empfangen.");
  if (server.method() == HTTP_GET) {
    String html = "<!DOCTYPE html><html><head><title>Tagespläne einstellen</title>";
    html += "<meta charset=\"UTF-8\">";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<style>";
    html += "body { font-family: 'Segoe UI', Arial, sans-serif; margin: 0; padding: 20px; background-color: #f5f5f5; }";
    html += "h1 { color: #333; text-align: center; margin-bottom: 30px; }";
    html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
    html += ".form-group { margin-bottom: 20px; }";
    html += "label { display: block; margin-bottom: 5px; font-weight: bold; }";
    html += "input[type='number'] { width: 60px; padding: 8px; margin: 4px; border: 1px solid #ddd; border-radius: 5px; }";
    html += "input[type='checkbox'] { margin-left: 10px; }";
    html += "input[type='submit'] { background: #007bff; color: white; border: none; padding: 12px 20px; border-radius: 5px; cursor: pointer; width: 100%; }";
    html += "input[type='submit']:hover { background: #0056b3; }";
    html += ".back-button { display: inline-block; padding: 12px 20px; background: #6c757d; color: white; text-decoration: none; border-radius: 5px; margin-top: 20px; transition: all 0.3s ease; }";
    html += ".back-button:hover { background: #5a6268; transform: translateX(-5px); box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
    html += ".day-section { background: #f8f9fa; padding: 20px; border-radius: 5px; margin: 20px 0; }";
    html += ".day-title { color: #007bff; margin-bottom: 15px; }";
    html += ".time-input { display: inline-block; margin-right: 20px; }";
    html += ".time-input label { display: block; margin-bottom: 5px; }";
    html += ".dimmed-option { margin-top: 10px; }";
    html += ".help-text { color: #666; font-size: 0.9em; margin-top: 5px; }";
    html += ".info-box { background: #e9ecef; padding: 15px; border-radius: 5px; margin: 20px 0; }";
    html += "</style>";
    html += "</head><body>";
    html += "<div class=\"container\">";
    html += "<h1>Tagespläne einstellen</h1>";
    html += "<div class=\"info-box\">";
    html += "<strong>Hinweis:</strong> Wenn der Dimmed-Modus nicht aktiviert ist, werden die LEDs komplett ausgeschaltet.";
    html += "</div>";
    html += "<form action=\"/configure_day_schedules\" method=\"post\">";

    const char* weekDays[7] = { "Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag" };
    for (int day = 0; day < 7; day++) {
      html += "<div class=\"day-section\">";
      html += "<h3 class=\"day-title\">" + String(weekDays[day]) + "</h3>";
      html += "<div class=\"time-input\">";
      html += "<label>Startzeit:</label>";
      html += "<input type=\"number\" name=\"day_" + String(day) + "_startHour\" min=\"0\" max=\"23\" value=\"" + String(weekSchedules[day].startHour) + "\">:";
      html += "<input type=\"number\" name=\"day_" + String(day) + "_startMinute\" min=\"0\" max=\"59\" value=\"" + String(weekSchedules[day].startMinute) + "\">";
      html += "</div>";
      html += "<div class=\"time-input\">";
      html += "<label>Endzeit:</label>";
      html += "<input type=\"number\" name=\"day_" + String(day) + "_endHour\" min=\"0\" max=\"23\" value=\"" + String(weekSchedules[day].endHour) + "\">:";
      html += "<input type=\"number\" name=\"day_" + String(day) + "_endMinute\" min=\"0\" max=\"59\" value=\"" + String(weekSchedules[day].endMinute) + "\">";
      html += "</div>";
      html += "<div class=\"dimmed-option\">";
      html += "<label>Dimmed-Modus: <input type=\"checkbox\" name=\"day_" + String(day) + "_dimmed\" " + String(weekSchedules[day].dimmed ? "checked" : "") + "></label>";
      html += "<div class=\"help-text\">Wenn aktiviert, werden die LEDs gedimmt statt ausgeschaltet.</div>";
      html += "</div>";
      html += "</div>";
    }

    html += "<input type=\"submit\" value=\"Speichern\">";
    html += "</form>";
    html += "<a href=\"/\" class=\"back-button\">Zurück zur Startseite</a>";
    html += "</div></body></html>";
    server.send(200, "text/html", html);
  } else if (server.method() == HTTP_POST) {
    for (int day = 0; day < 7; day++) {
      if (server.hasArg("day_" + String(day) + "_startHour") && server.hasArg("day_" + String(day) + "_startMinute") && server.hasArg("day_" + String(day) + "_endHour") && server.hasArg("day_" + String(day) + "_endMinute")) {

        weekSchedules[day].startHour = server.arg("day_" + String(day) + "_startHour").toInt();
        weekSchedules[day].startMinute = server.arg("day_" + String(day) + "_startMinute").toInt();
        weekSchedules[day].endHour = server.arg("day_" + String(day) + "_endHour").toInt();
        weekSchedules[day].endMinute = server.arg("day_" + String(day) + "_endMinute").toInt();
        weekSchedules[day].dimmed = server.hasArg("day_" + String(day) + "_dimmed");

        // Speichern
        String keyStartHour = "day_" + String(day) + "_startHour";
        String keyStartMinute = "day_" + String(day) + "_startMinute";
        String keyEndHour = "day_" + String(day) + "_endHour";
        String keyEndMinute = "day_" + String(day) + "_endMinute";
        String keyDimmed = "day_" + String(day) + "_dimmed";

        preferences.putInt(keyStartHour.c_str(), weekSchedules[day].startHour);
        preferences.putInt(keyStartMinute.c_str(), weekSchedules[day].startMinute);
        preferences.putInt(keyEndHour.c_str(), weekSchedules[day].endHour);
        preferences.putInt(keyEndMinute.c_str(), weekSchedules[day].endMinute);
        preferences.putBool(keyDimmed.c_str(), weekSchedules[day].dimmed);
      }
    }
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  }
}

// --------------------------------------------------------
//  Update-Seite
// --------------------------------------------------------
void handleUpdatePage() {
  String html = "<!DOCTYPE html><html><head><title>Online Update</title>";
  html += "<meta charset=\"UTF-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<style>";
  html += "body { font-family: 'Segoe UI', Arial, sans-serif; margin: 0; padding: 20px; background-color: #f5f5f5; }";
  html += "h1 { color: #333; text-align: center; margin-bottom: 30px; }";
  html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
  html += ".form-group { margin-bottom: 20px; }";
  html += "label { display: block; margin-bottom: 5px; font-weight: bold; }";
  html += "input[type='text'] { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 5px; margin-bottom: 10px; }";
  html += "input[type='submit'] { background: #007bff; color: white; border: none; padding: 12px 20px; border-radius: 5px; cursor: pointer; width: 100%; }";
  html += "input[type='submit']:hover { background: #0056b3; }";
  html += ".back-button { display: inline-block; padding: 12px 20px; background: #6c757d; color: white; text-decoration: none; border-radius: 5px; margin-top: 20px; transition: all 0.3s ease; }";
  html += ".back-button:hover { background: #5a6268; transform: translateX(-5px); box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
  html += ".update-button { background: #28a745; margin-top: 20px; }";
  html += ".update-button:hover { background: #218838; }";
  html += ".current-url { background: #f8f9fa; padding: 15px; border-radius: 5px; margin: 20px 0; word-break: break-all; }";
  html += ".warning { color: #dc3545; font-weight: bold; margin: 20px 0; padding: 15px; background: #f8d7da; border-radius: 5px; }";
  html += "</style>";
  html += "</head><body>";
  html += "<div class=\"container\">";
  html += "<h1>Online Update</h1>";
  html += "<div class=\"warning\">⚠️ Wichtig: Während des Updates wird die Uhr kurzzeitig neu gestartet. Bitte warten Sie, bis der Vorgang abgeschlossen ist.</div>";
  html += "<form action=\"/set_firmware_url\" method=\"post\">";
  html += "<div class=\"form-group\">";
  html += "<label>Firmware-URL:</label>";
  html += "<input type=\"text\" name=\"firmwareurl\" value=\"" + firmwareURL + "\">";
  html += "</div>";
  html += "<input type=\"submit\" value=\"URL speichern\">";
  html += "</form>";
  html += "<div class=\"current-url\">";
  html += "<strong>Aktuelle URL:</strong><br>" + firmwareURL;
  html += "</div>";
  html += "<a href=\"/do_update\" class=\"back-button update-button\">Jetzt Update einspielen</a>";
  html += "<a href=\"/\" class=\"back-button\">Zurück zur Startseite</a>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleSetFirmwareURL() {
  if (server.hasArg("firmwareurl")) {
    firmwareURL = server.arg("firmwareurl");
    preferences.putString("firmwareURL", firmwareURL);
    Serial.println("Firmware-URL aktualisiert auf: " + firmwareURL);
  }
  server.sendHeader("Location", "/update", true);
  server.send(302, "text/plain", "");
}

void handleDoUpdate() {
  server.send(200, "text/plain", "Update wird gestartet... Bitte warten!");
  HTTPClient httpClient;
  httpClient.begin(firmwareURL);
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
      break;
  }
  httpClient.end();
}

// --------------------------------------------------------
//  IP-Konfiguration
// --------------------------------------------------------
void handleConfigureIP() {
  Serial.println("Anfrage an handleConfigureIP empfangen.");
  if (server.hasArg("mode")) {
    String mode = server.arg("mode");
    if (mode == "dhcp") {
      useDHCP = true;
    } else {
      useDHCP = false;
      if (server.hasArg("ip")) staticIP = server.arg("ip");
      if (server.hasArg("gateway")) staticGateway = server.arg("gateway");
      if (server.hasArg("subnet")) staticSubnet = server.arg("subnet");
    }
    preferences.putBool("useDHCP", useDHCP);
    if (!useDHCP) {
      preferences.putString("staticIP", staticIP);
      preferences.putString("staticGateway", staticGateway);
      preferences.putString("staticSubnet", staticSubnet);
    }
  }
  server.sendHeader("Location", "/wifi", true);
  server.send(302, "text/plain", "");
}

// --------------------------------------------------------
//  Sommer-/Winterzeit-Einstellung
// --------------------------------------------------------
void handleDST() {
  if (server.method() == HTTP_GET) {
    String html = "<!DOCTYPE html><html><head><title>Sommer-/Winterzeit</title>";
    html += "<meta charset=\"UTF-8\">";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<style>body { font-family: Arial, sans-serif; margin: 20px; }";
    html += ".switch { position: relative; display: inline-block; width: 60px; height: 34px; }";
    html += ".switch input { opacity: 0; width: 0; height: 0; }";
    html += ".slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; border-radius: 34px; }";
    html += ".slider:before { position: absolute; content: \"\"; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }";
    html += "input:checked + .slider { background-color: #2196F3; }";
    html += "input:checked + .slider:before { transform: translateX(26px); }";
    html += "</style></head><body>";
    html += "<h1>Sommer-/Winterzeit Einstellung</h1>";
    html += "<form action=\"/dst\" method=\"post\">";
    html += "<p>Aktuelle Einstellung: <strong>" + String(isDST ? "Sommerzeit" : "Winterzeit") + "</strong></p>";
    html += "<label class=\"switch\">";
    html += "<input type=\"checkbox\" name=\"dst\" " + String(isDST ? "checked" : "") + ">";
    html += "<span class=\"slider\"></span>";
    html += "</label>";
    html += "<p>Winterzeit <-> Sommerzeit</p>";
    html += "<br><input type=\"submit\" value=\"Speichern\">";
    html += "</form>";
    html += "<p><a href=\"/\">Zurück zur Startseite</a></p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  } else if (server.method() == HTTP_POST) {
    bool newDST = server.hasArg("dst");
    if (isDST != newDST) {
      isDST = newDST;
      preferences.putBool("isDST", isDST);
      
      // Zeit um eine Stunde vor- oder zurückstellen
      if (rtcAvailable) {
        DateTime now = rtc.now();
        if (isDST) {
          // Sommerzeit: +1 Stunde
          rtc.adjust(DateTime(now.year(), now.month(), now.day(), 
                            (now.hour() + 1) % 24, now.minute(), now.second()));
        } else {
          // Winterzeit: -1 Stunde
          rtc.adjust(DateTime(now.year(), now.month(), now.day(), 
                            (now.hour() + 23) % 24, now.minute(), now.second()));
        }
      }
      
      Serial.println(isDST ? "Auf Sommerzeit umgestellt" : "Auf Winterzeit umgestellt");
    }
    server.sendHeader("Location", "/dst", true);
    server.send(302, "text/plain", "");
  }
}

// --------------------------------------------------------
//  Neue Funktion für Zeitzoneneinstellung
// --------------------------------------------------------
void handleTimezone() {
  if (server.method() == HTTP_GET) {
    String html = "<!DOCTYPE html><html><head><title>Zeitzone einstellen</title>";
    html += "<meta charset=\"UTF-8\">";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<style>";
    html += "body { font-family: 'Segoe UI', Arial, sans-serif; margin: 0; padding: 20px; background-color: #f5f5f5; }";
    html += "h1 { color: #333; text-align: center; margin-bottom: 30px; }";
    html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
    html += ".form-group { margin-bottom: 20px; }";
    html += "label { display: block; margin-bottom: 5px; font-weight: bold; }";
    html += "select { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 5px; margin-bottom: 10px; }";
    html += "input[type='submit'] { background: #007bff; color: white; border: none; padding: 12px 20px; border-radius: 5px; cursor: pointer; width: 100%; }";
    html += "input[type='submit']:hover { background: #0056b3; }";
    html += ".back-button { display: inline-block; padding: 12px 20px; background: #6c757d; color: white; text-decoration: none; border-radius: 5px; margin-top: 20px; transition: all 0.3s ease; }";
    html += ".back-button:hover { background: #5a6268; transform: translateX(-5px); box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
    html += ".help-text { color: #666; font-size: 0.9em; margin-top: 5px; }";
    html += "</style>";
    html += "</head><body>";
    html += "<div class=\"container\">";
    html += "<h1>Zeitzone einstellen</h1>";
    html += "<form action=\"/timezone\" method=\"post\">";
    html += "<div class=\"form-group\">";
    html += "<label>Zeitzone:</label>";
    html += "<select name=\"timezone\">";
    html += "<option value=\"CET-1CEST,M3.5.0,M10.5.0/3\">Mitteleuropäische Zeit (MEZ/MESZ)</option>";
    html += "<option value=\"UTC\">UTC</option>";
    html += "<option value=\"GMT-1\">GMT-1 (Azoren)</option>";
    html += "<option value=\"GMT-2\">GMT-2 (Südgeorgien)</option>";
    html += "<option value=\"GMT-3\">GMT-3 (Brasilien)</option>";
    html += "<option value=\"GMT-4\">GMT-4 (Atlantik)</option>";
    html += "<option value=\"GMT-5\">GMT-5 (Ostküste USA)</option>";
    html += "<option value=\"GMT-6\">GMT-6 (Zentral USA)</option>";
    html += "<option value=\"GMT-7\">GMT-7 (Rocky Mountains)</option>";
    html += "<option value=\"GMT-8\">GMT-8 (Pazifik)</option>";
    html += "<option value=\"GMT-9\">GMT-9 (Alaska)</option>";
    html += "<option value=\"GMT-10\">GMT-10 (Hawaii)</option>";
    html += "</select>";
    html += "<div class=\"help-text\">Wählen Sie Ihre lokale Zeitzone aus.</div>";
    html += "</div>";
    html += "<input type=\"submit\" value=\"Speichern\">";
    html += "</form>";
    html += "<a href=\"/\" class=\"back-button\">Zurück zur Startseite</a>";
    html += "</div></body></html>";
    server.send(200, "text/html", html);
  } else if (server.method() == HTTP_POST) {
    if (server.hasArg("timezone")) {
      String timezone = server.arg("timezone");
      configTzTime(timezone.c_str(), "pool.ntp.org");
      preferences.putString("timezone", timezone);
      server.sendHeader("Location", "/", true);
      server.send(302, "text/plain", "");
    }
  }
}

// --------------------------------------------------------
//  Neue Funktion für Dimm-Helligkeitseinstellung
// --------------------------------------------------------
void handleDimBrightness() {
  if (server.method() == HTTP_GET) {
    String html = "<!DOCTYPE html><html><head><title>Dimm-Helligkeit einstellen</title>";
    html += "<meta charset=\"UTF-8\">";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<style>";
    html += "body { font-family: 'Segoe UI', Arial, sans-serif; margin: 0; padding: 20px; background-color: #f5f5f5; }";
    html += "h1 { color: #333; text-align: center; margin-bottom: 30px; }";
    html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
    html += ".form-group { margin-bottom: 20px; }";
    html += "label { display: block; margin-bottom: 5px; font-weight: bold; }";
    html += ".preview-box { width: 100%; height: 100px; background: #333; margin: 20px 0; border-radius: 5px; display: flex; align-items: center; justify-content: center; }";
    html += ".preview-text { color: white; font-size: 24px; }";
    html += "input[type='range'] { width: 100%; margin: 10px 0; }";
    html += "input[type='number'] { width: 80px; padding: 8px; margin: 4px; border: 1px solid #ddd; border-radius: 5px; }";
    html += "input[type='submit'] { background: #007bff; color: white; border: none; padding: 12px 20px; border-radius: 5px; cursor: pointer; width: 100%; }";
    html += "input[type='submit']:hover { background: #0056b3; }";
    html += ".back-button { display: inline-block; padding: 12px 20px; background: #6c757d; color: white; text-decoration: none; border-radius: 5px; margin-top: 20px; transition: all 0.3s ease; }";
    html += ".back-button:hover { background: #5a6268; transform: translateX(-5px); box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
    html += ".help-text { color: #666; font-size: 0.9em; margin-top: 5px; }";
    html += "</style>";
    html += "<script>";
    html += "function updatePreview(value) {";
    html += "  document.getElementById('preview').style.opacity = value / 100;";
    html += "  document.getElementById('brightnessValue').value = value;";
    html += "}";
    html += "</script>";
    html += "</head><body>";
    html += "<div class=\"container\">";
    html += "<h1>Dimm-Helligkeit einstellen</h1>";
    html += "<div class=\"preview-box\" id=\"preview\" style=\"opacity: " + String(dimBrightness) + "/100;\">";
    html += "<div class=\"preview-text\">Vorschau</div>";
    html += "</div>";
    html += "<form action=\"/dim_brightness\" method=\"post\">";
    html += "<div class=\"form-group\">";
    html += "<label>Helligkeit (0-100):</label>";
    html += "<input type=\"range\" name=\"dimBrightness\" min=\"0\" max=\"100\" value=\"" + String(dimBrightness) + "\" oninput=\"updatePreview(this.value)\">";
    html += "<input type=\"number\" id=\"brightnessValue\" name=\"dimBrightness\" min=\"0\" max=\"100\" value=\"" + String(dimBrightness) + "\" oninput=\"updatePreview(this.value)\">";
    html += "<div class=\"help-text\">Stellen Sie die Helligkeit der LEDs im gedimmten Zustand ein. 0 = aus, 100 = maximale Helligkeit.</div>";
    html += "</div>";
    html += "<input type=\"submit\" value=\"Speichern\">";
    html += "</form>";
    html += "<a href=\"/\" class=\"back-button\">Zurück zur Startseite</a>";
    html += "</div></body></html>";
    server.send(200, "text/html", html);
  } else if (server.method() == HTTP_POST) {
    if (server.hasArg("dimBrightness")) {
      dimBrightness = server.arg("dimBrightness").toInt();
      preferences.putInt("dimBrightness", dimBrightness);
    }
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  }
}

// ========================================================
//   Wortuhr-Anzeige (displayTime)
// ========================================================
void displayTime() {
  static int lastMinute = -1;

  clearLEDs();

  struct tm timeInfo;
  bool haveTime = false;

  // 1) NTP + Offset (falls WLAN da)
  if (WiFi.status() == WL_CONNECTED) {
    if (getLocalTime(&timeInfo)) {
      timeInfo.tm_min += timeOffsetMinutes;
      mktime(&timeInfo);
      haveTime = true;
      // Software-Uhr aktualisieren
      softwareTime = mktime(&timeInfo);
      lastMillisForSoftwareClock = millis();
      isTimeValid = true;

      // Debug
      Serial.println("displayTime(): NTP-Zeit benutzt.");
    }
  }

  // 2) Falls kein NTP => DS3231
  if (!haveTime && rtcAvailable) {
    DateTime now = rtc.now();
    timeInfo.tm_year = now.year() - 1900;
    timeInfo.tm_mon = now.month() - 1;
    timeInfo.tm_mday = now.day();
    timeInfo.tm_hour = now.hour();
    timeInfo.tm_min = now.minute();
    timeInfo.tm_sec = now.second();
    
    // Offset und DST berücksichtigen
    timeInfo.tm_min += timeOffsetMinutes;
    if (!WiFi.status() == WL_CONNECTED && isDST) {
      timeInfo.tm_hour = (timeInfo.tm_hour + 1) % 24;  // DST nur berücksichtigen wenn offline
    }
    mktime(&timeInfo);

    haveTime = true;
    // Software-Uhr aktualisieren
    softwareTime = mktime(&timeInfo);
    lastMillisForSoftwareClock = millis();
    isTimeValid = true;

    Serial.println("displayTime(): DS3231 benutzt.");
  }

  // 3) Falls weder NTP noch DS3231 => Software-Uhr (wenn wir jemals eine gültige Zeit hatten)
  if (!haveTime) {
    if (isTimeValid) {
      // Zeit fortschreiben seit letztem Loop
      unsigned long nowMillis = millis();
      unsigned long deltaMillis = nowMillis - lastMillisForSoftwareClock;
      lastMillisForSoftwareClock = nowMillis;

      // Erhöhe softwareTime um deltaMillis in Sekunden
      time_t deltaSeconds = (time_t)(deltaMillis / 1000);
      softwareTime += deltaSeconds;

      // Dann in struct tm konvertieren, Offset noch draufschlagen
      time_t localT = softwareTime + (timeOffsetMinutes * 60);
      struct tm* softTm = localtime(&localT);
      timeInfo = *softTm;
      haveTime = true;

      Serial.println("displayTime(): Software-Uhr benutzt.");
    } else {
      // Keine gültige Zeit, brechen ab
      Serial.println("Keine gültige Zeit, beende displayTime().");
      strip.Show();
      return;
    }
  }

  // Ab hier haben wir eine gültige timeInfo
  int currentHour = timeInfo.tm_hour;
  int currentMinute = timeInfo.tm_min;
  int currentWday = timeInfo.tm_wday;


  // Regenbogenaktualisierung pro Minute
  if (rainbowMode && currentMinute != lastMinute) {
    lastMinute = currentMinute;
    for (int i = 0; i < NUM_WORDS; i++) {
      wordHues[i] = random(0, 360);
    }
    Serial.println("Regenbogen-Hue neu gemischt (Minutenwechsel).");
  }

  // Tagesplan: Helligkeit / Dimmed / Aus
  DaySchedule currentSchedule = weekSchedules[currentWday];
  int currentTimeInMinutes = currentHour * 60 + currentMinute;
  int startTimeInMinutes = currentSchedule.startHour * 60 + currentSchedule.startMinute;
  int endTimeInMinutes = currentSchedule.endHour * 60 + currentSchedule.endMinute;
  bool isDaytime = false;

  if (startTimeInMinutes < endTimeInMinutes) {
    if (currentTimeInMinutes >= startTimeInMinutes && currentTimeInMinutes < endTimeInMinutes)
      isDaytime = true;
  } else {
    // Falls Start > End => z. B. 22:00 bis 06:00
    if (currentTimeInMinutes >= startTimeInMinutes || currentTimeInMinutes < endTimeInMinutes)
      isDaytime = true;
  }

  if (isDaytime) {
    strip.SetBrightness(brightness);
  } else {
    if (currentSchedule.dimmed) {
      // Skaliere dimBrightness (0-100) auf den tatsächlichen Helligkeitsbereich (0-255)
      uint8_t scaledDimBrightness = map(dimBrightness, 0, 100, 0, brightness);
      strip.SetBrightness(scaledDimBrightness);
    } else {
      strip.SetBrightness(0);
    }
  }

  Serial.printf("Aktuelle Zeit: %02d:%02d (Wochentag=%d)\n", currentHour, currentMinute, currentWday);

  int displayHour = currentHour % 12;
  if (displayHour == 0) displayHour = 12;
  int hourIndex = displayHour - 1;

  int hourWordIndices[12] = {
    WORD_EIN,
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
    WORD_ZWOELF
  };

  if (currentMinute == 0) {
    setWordWithPriority(ES, getColorForWord(WORD_ES));
    setWordWithPriority(IST, getColorForWord(WORD_IST));
    setWordWithPriority(STUNDENADR[hourIndex], getColorForWord((WordIndex)hourWordIndices[hourIndex]), true);
    setWordWithPriority(HalfFullHourAddr[1], getColorForWord(WORD_UHR));
  } else if (currentMinute > 0 && currentMinute < 5) {
    setWordWithPriority(ES, getColorForWord(WORD_ES));
    setWordWithPriority(IST, getColorForWord(WORD_IST));
    setWordWithPriority(STUNDENADR[hourIndex], getColorForWord((WordIndex)hourWordIndices[hourIndex]), true);
    setWordWithPriority(HalfFullHourAddr[1], getColorForWord(WORD_UHR));

    setSingleLED(LED_PLUS, getColorForWord(WORD_PLUS));
    setSingleLED(LED_1, RgbColor(0, 0, 0));
    setSingleLED(LED_2, RgbColor(0, 0, 0));
    setSingleLED(LED_3, RgbColor(0, 0, 0));
    setSingleLED(LED_4, RgbColor(0, 0, 0));

    switch (currentMinute) {
      case 1:
        setSingleLED(LED_1, getColorForWord(WORD_NUM_1));
        setWordWithPriority(MINUTE, getColorForWord(WORD_MINUTE));
        break;
      case 2:
        setSingleLED(LED_2, getColorForWord(WORD_NUM_2));
        setWordWithPriority(MINUTE, getColorForWord(WORD_MINUTE));
        setWordWithPriority(MINUTEN, getColorForWord(WORD_MINUTEN), true);
        break;
      case 3:
        setSingleLED(LED_3, getColorForWord(WORD_NUM_3));
        setWordWithPriority(MINUTE, getColorForWord(WORD_MINUTE));
        setWordWithPriority(MINUTEN, getColorForWord(WORD_MINUTEN), true);
        break;
      case 4:
        setSingleLED(LED_4, getColorForWord(WORD_NUM_4));
        setWordWithPriority(MINUTE, getColorForWord(WORD_MINUTE));
        setWordWithPriority(MINUTEN, getColorForWord(WORD_MINUTEN), true);
        break;
    }
  } else {
    int baseMinutes = (currentMinute / 5) * 5;
    int extraMinutes = currentMinute % 5;

    switch (baseMinutes) {
      case 5:
        setWordWithPriority(ES, getColorForWord(WORD_ES));
        setWordWithPriority(IST, getColorForWord(WORD_IST));
        setWordWithPriority(MINUTENADR[0], getColorForWord(WORD_FUENF_M));
        setWordWithPriority(ToPastAddr[1], getColorForWord(WORD_NACH));
        break;
      case 10:
        setWordWithPriority(ES, getColorForWord(WORD_ES));
        setWordWithPriority(IST, getColorForWord(WORD_IST));
        setWordWithPriority(MINUTENADR[1], getColorForWord(WORD_ZEHN_M));
        setWordWithPriority(ToPastAddr[1], getColorForWord(WORD_NACH));
        break;
      case 15:
        setWordWithPriority(ES, getColorForWord(WORD_ES));
        setWordWithPriority(IST, getColorForWord(WORD_IST));
        setWordWithPriority(MINUTENADR[2], getColorForWord(WORD_VIERTEL));
        setWordWithPriority(ToPastAddr[1], getColorForWord(WORD_NACH));
        break;
      case 20:
        setWordWithPriority(ES, getColorForWord(WORD_ES));
        setWordWithPriority(IST, getColorForWord(WORD_IST));
        setWordWithPriority(MINUTENADR[3], getColorForWord(WORD_ZWANZIG));
        setWordWithPriority(ToPastAddr[1], getColorForWord(WORD_NACH));
        break;
      case 25:
        setWordWithPriority(ES, getColorForWord(WORD_ES));
        setWordWithPriority(IST, getColorForWord(WORD_IST));
        setWordWithPriority(MINUTENADR[0], getColorForWord(WORD_FUENF_M));
        setWordWithPriority(ToPastAddr[0], getColorForWord(WORD_VOR));
        setWordWithPriority(HalfFullHourAddr[0], getColorForWord(WORD_HALB));
        displayHour = (currentHour % 12) + 1;
        if (displayHour == 13) displayHour = 1;
        hourIndex = displayHour - 1;
        break;
      case 30:
        setWordWithPriority(ES, getColorForWord(WORD_ES));
        setWordWithPriority(IST, getColorForWord(WORD_IST));
        setWordWithPriority(HalfFullHourAddr[0], getColorForWord(WORD_HALB));
        displayHour = (currentHour % 12) + 1;
        if (displayHour == 13) displayHour = 1;
        hourIndex = displayHour - 1;
        break;
      case 35:
        setWordWithPriority(ES, getColorForWord(WORD_ES));
        setWordWithPriority(IST, getColorForWord(WORD_IST));
        setWordWithPriority(MINUTENADR[0], getColorForWord(WORD_FUENF_M));
        setWordWithPriority(ToPastAddr[1], getColorForWord(WORD_NACH));
        setWordWithPriority(HalfFullHourAddr[0], getColorForWord(WORD_HALB));
        displayHour = (currentHour % 12) + 1;
        if (displayHour == 13) displayHour = 1;
        hourIndex = displayHour - 1;
        break;
      case 40:
        setWordWithPriority(ES, getColorForWord(WORD_ES));
        setWordWithPriority(IST, getColorForWord(WORD_IST));
        setWordWithPriority(MINUTENADR[3], getColorForWord(WORD_ZWANZIG));
        setWordWithPriority(ToPastAddr[0], getColorForWord(WORD_VOR));
        displayHour = (currentHour % 12) + 1;
        if (displayHour == 13) displayHour = 1;
        hourIndex = displayHour - 1;
        break;
      case 45:
        setWordWithPriority(ES, getColorForWord(WORD_ES));
        setWordWithPriority(IST, getColorForWord(WORD_IST));
        setWordWithPriority(MINUTENADR[2], getColorForWord(WORD_VIERTEL));
        setWordWithPriority(ToPastAddr[0], getColorForWord(WORD_VOR));
        displayHour = (currentHour % 12) + 1;
        if (displayHour == 13) displayHour = 1;
        hourIndex = displayHour - 1;
        break;
      case 50:
        setWordWithPriority(ES, getColorForWord(WORD_ES));
        setWordWithPriority(IST, getColorForWord(WORD_IST));
        setWordWithPriority(MINUTENADR[1], getColorForWord(WORD_ZEHN_M));
        setWordWithPriority(ToPastAddr[0], getColorForWord(WORD_VOR));
        displayHour = (currentHour % 12) + 1;
        if (displayHour == 13) displayHour = 1;
        hourIndex = displayHour - 1;
        break;
      case 55:
        setWordWithPriority(ES, getColorForWord(WORD_ES));
        setWordWithPriority(IST, getColorForWord(WORD_IST));
        setWordWithPriority(MINUTENADR[0], getColorForWord(WORD_FUENF_M));
        setWordWithPriority(ToPastAddr[0], getColorForWord(WORD_VOR));
        displayHour = (currentHour % 12) + 1;
        if (displayHour == 13) displayHour = 1;
        hourIndex = displayHour - 1;
        break;
      default:
        // Nichts
        break;
    }

    // "Eins" statt "Ein" bei Minuten >= 5
    if (displayHour == 1 && baseMinutes >= 5) {
      setWordWithPriority(EINS, getColorForWord(WORD_EINS), true);
    } else {
      setWordWithPriority(STUNDENADR[hourIndex], getColorForWord((WordIndex)hourWordIndices[hourIndex]), true);
    }

    // Extra-Minuten (1..4)
    if (extraMinutes >= 1 && extraMinutes <= 4) {
      setSingleLED(LED_PLUS, getColorForWord(WORD_PLUS));
      setSingleLED(LED_1, RgbColor(0, 0, 0));
      setSingleLED(LED_2, RgbColor(0, 0, 0));
      setSingleLED(LED_3, RgbColor(0, 0, 0));
      setSingleLED(LED_4, RgbColor(0, 0, 0));

      if (extraMinutes >= 2) {
        setWordWithPriority(MINUTEN, getColorForWord(WORD_MINUTEN), true);
      }

      switch (extraMinutes) {
        case 1:
          setSingleLED(LED_1, getColorForWord(WORD_NUM_1));
          setWordWithPriority(MINUTE, getColorForWord(WORD_MINUTE));
          break;
        case 2:
          setSingleLED(LED_2, getColorForWord(WORD_NUM_2));
          setWordWithPriority(MINUTE, getColorForWord(WORD_MINUTE));
          break;
        case 3:
          setSingleLED(LED_3, getColorForWord(WORD_NUM_3));
          setWordWithPriority(MINUTE, getColorForWord(WORD_MINUTE));
          break;
        case 4:
          setSingleLED(LED_4, getColorForWord(WORD_NUM_4));
          setWordWithPriority(MINUTE, getColorForWord(WORD_MINUTE));
          break;
      }
    } else {
      setSingleLED(LED_PLUS, RgbColor(0, 0, 0));
      setSingleLED(LED_1, RgbColor(0, 0, 0));
      setSingleLED(LED_2, RgbColor(0, 0, 0));
      setSingleLED(LED_3, RgbColor(0, 0, 0));
      setSingleLED(LED_4, RgbColor(0, 0, 0));
      setWordWithPriority(MINUTE, RgbColor(0, 0, 0));
      setWordWithPriority(MINUTEN, RgbColor(0, 0, 0));
    }
  }

  strip.Show();
}

// --------------------------------------------------------
//   LED-Snake-Animation (ledStartTest)
//   Es leuchten immer höchstens 10 LEDs gleichzeitig.
// --------------------------------------------------------
void ledStartTest() {
  Serial.println("Starte LED Snake-Animation...");
  clearLEDs();
  const int maxOn = 10;  // maximal 10 LEDs gleichzeitig
  
  // Die "Snake" schaltet LED für LED ein. Sobald mehr als 10 LEDs an sind,
  // wird die älteste wieder ausgeschaltet.
  for (int i = 0; i < NUM_LEDS; i++) {
    setSingleLED(i, RgbColor(255, 255, 255));  // LED einschalten
    if (i >= maxOn) {
      setSingleLED(i - maxOn, RgbColor(0, 0, 0));  // LED ausschalten, die schon länger leuchtet
    }
    strip.Show();
    delay(50);
  }
  // Schalte die letzten noch leuchtenden LEDs aus
  for (int i = NUM_LEDS - maxOn; i < NUM_LEDS; i++) {
    setSingleLED(i, RgbColor(0, 0, 0));
    strip.Show();
    delay(50);
  }
  Serial.println("LED Snake-Animation abgeschlossen.");
}

// Neue Funktion für WLAN-Verbindungsversuch
bool tryWiFiConnection() {
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");
  
  if (ssid == "") {
    return false;
  }

  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.print("Versuche, mit WLAN zu verbinden: ");
  Serial.println(ssid);
  
  WiFi.disconnect();
  delay(500);
  
  if (!useDHCP) {
    IPAddress ip, gateway, subnet, dns1, dns2;
    ip.fromString(staticIP);
    gateway.fromString(staticGateway);
    subnet.fromString(staticSubnet);
    dns1.fromString("8.8.8.8");
    dns2.fromString("1.1.1.1");
    if (!WiFi.config(ip, gateway, subnet, dns1, dns2)) {
      Serial.println("FEHLER: Konnte statische IP nicht setzen!");
    }
  }
  
  WiFi.begin(ssid.c_str(), password.c_str());
  
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
    return true;
  }
  
  Serial.println("\nKonnte nicht mit WLAN verbinden.");
  return false;
}

// ========================================================
//   Setup
// ========================================================
void setup() {
  Serial.begin(115200);
  Serial.println("Starte Word Clock...");

  randomSeed(analogRead(0));

  Serial.println("Initialisiere Preferences...");
  preferences.begin("settings", false);
  Serial.println("Preferences initialisiert.");

  initialTimeSet = preferences.getBool("initialTimeSet", false);

  // DHCP oder statische IP
  useDHCP = preferences.getBool("useDHCP", true);
  staticIP = preferences.getString("staticIP", "192.168.4.50");
  staticGateway = preferences.getString("staticGateway", "192.168.4.1");
  staticSubnet = preferences.getString("staticSubnet", "255.255.255.0");

  // HSV-Werte initialisieren
  initializeWordHues();

  // Einstellungen aus Preferences
  brightness = preferences.getUChar("brightness", 50);
  uint32_t colorValue = preferences.getUInt("color", 0xFFFFFF);
  selectedColor = RgbColor((colorValue >> 16) & 0xFF, (colorValue >> 8) & 0xFF, colorValue & 0xFF);
  rainbowMode = preferences.getBool("rainbow", true);
  dimBrightness = preferences.getUChar("dimBrightness", 25);  // Lade Dimm-Helligkeit aus Preferences

  timeOffsetMinutes = preferences.getInt("timeOffset", 0);

  clearLEDs();

  // Tagespläne laden
  for (int day = 0; day < 7; day++) {
    String keyStartHour = "day_" + String(day) + "_startHour";
    String keyStartMinute = "day_" + String(day) + "_startMinute";
    String keyEndHour = "day_" + String(day) + "_endHour";
    String keyEndMinute = "day_" + String(day) + "_endMinute";
    String keyDimmed = "day_" + String(day) + "_dimmed";

    weekSchedules[day].startHour = preferences.getInt(keyStartHour.c_str(), 8);
    weekSchedules[day].startMinute = preferences.getInt(keyStartMinute.c_str(), 0);
    weekSchedules[day].endHour = preferences.getInt(keyEndHour.c_str(), 22);
    weekSchedules[day].endMinute = preferences.getInt(keyEndMinute.c_str(), 0);
    weekSchedules[day].dimmed = preferences.getBool(keyDimmed.c_str(), true);
  }

  // WLAN + Access Point
  WiFi.mode(WIFI_AP_STA);
  bool apStarted = WiFi.softAP("WordClock", "password123", 1, false, 10);
  if (apStarted) {
    Serial.print("Access Point gestartet. AP IP: ");
    Serial.println(WiFi.softAPIP());
  }

  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");

  // Statische IP, falls gewünscht
  if (!useDHCP) {
    IPAddress ip, gateway, subnet, dns1, dns2;
    ip.fromString(staticIP);
    gateway.fromString(staticGateway);
    subnet.fromString(staticSubnet);
    dns1.fromString("8.8.8.8");
    dns2.fromString("1.1.1.1");
    if (!WiFi.config(ip, gateway, subnet, dns1, dns2)) {
      Serial.println("FEHLER: Konnte statische IP nicht setzen!");
    } else {
      Serial.println("Statische IP konfiguriert: " + staticIP);
    }
  }

  // Optionaler dritter DNS
  ip_addr_t addr;
  addr.u_addr.ip4.addr = ipaddr_addr("192.168.49.101");
  addr.type = IPADDR_TYPE_V4;
  dns_setserver(2, &addr);

  // Falls SSID gespeichert, versuche verbinden
  if (ssid != "") {
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.print("Versuche, mit WLAN zu verbinden: ");
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

      // Falls noch nie Zeit gesetzt, warte auf NTP und stelle DS3231
      if (!initialTimeSet) {
        struct tm timeInfo;
        int retryTime = 0;
        while (!getLocalTime(&timeInfo) && retryTime < 10) {
          Serial.println("Warte auf NTP (Erststart)...");
          delay(1000);
          retryTime++;
        }
        if (getLocalTime(&timeInfo)) {
          initialTimeSet = true;
          preferences.putBool("initialTimeSet", initialTimeSet);

          if (!rtc.begin()) {
            Serial.println("ACHTUNG: Kein DS3231 gefunden!");
            rtcAvailable = false;
          } else {
            rtcAvailable = true;
            if (rtc.lostPower()) {
              Serial.println("DS3231 hatte Stromausfall, stelle erneut ein!");
            }
            rtc.adjust(DateTime(
              timeInfo.tm_year + 1900,
              timeInfo.tm_mon + 1,
              timeInfo.tm_mday,
              timeInfo.tm_hour,
              timeInfo.tm_min,
              timeInfo.tm_sec));
            Serial.println("DS3231 mit NTP-Zeit (Erststart) aktualisiert.");
          }

          // Software-Uhr
          softwareTime = mktime(&timeInfo);
          lastMillisForSoftwareClock = millis();
          isTimeValid = true;
        }
      } else {
        if (!rtc.begin()) {
          Serial.println("ACHTUNG: Kein DS3231 gefunden!");
          rtcAvailable = false;
        } else {
          rtcAvailable = true;
          if (rtc.lostPower()) {
            Serial.println("DS3231 hatte Stromausfall, bitte warten auf NTP!");
          }
        }
        // Falls Zeit schon mal gesetzt war, holen wir uns NTP
        struct tm timeInfo;
        if (getLocalTime(&timeInfo)) {
          softwareTime = mktime(&timeInfo);
          lastMillisForSoftwareClock = millis();
          isTimeValid = true;
        }
      }
    } else {
      Serial.println("\nKonnte nicht mit WLAN verbinden.");
      // RTC initialisieren
      if (!rtc.begin()) {
        Serial.println("ACHTUNG: Kein DS3231 gefunden!");
        rtcAvailable = false;
      } else {
        rtcAvailable = true;
        if (rtc.lostPower()) {
          Serial.println("DS3231 hatte Stromausfall, bitte Zeit einstellen (via NTP)!");
        }
      }
    }
  } else {
    // Keine SSID
    Serial.println("Keine SSID im Speicher, gehe offline vor.");
    if (!rtc.begin()) {
      Serial.println("ACHTUNG: Kein DS3231 gefunden!");
      rtcAvailable = false;
    } else {
      rtcAvailable = true;
      if (rtc.lostPower()) {
        Serial.println("DS3231 hatte Stromausfall, bitte Zeit einstellen (via NTP)!");
      }
    }
  }

  // NeoPixel-Strip
  strip.Begin();
  strip.Show();

  // Webserver-Routen
  server.on("/", handleRoot);
  server.on("/diagnose", handleDiagnose);
  server.on("/configure_main", handleConfigureMain);
  server.on("/wifi", handleWifi);
  server.on("/configure_wifi", handleConfigureWifi);
  server.on("/set_time_offset", handleSetTimeOffset);
  server.on("/configure_day_schedules", handleConfigureDaySchedules);
  server.on("/reset", handleReset);
  server.on("/confirm_reset", handleConfirmReset);
  server.on("/update", handleUpdatePage);
  server.on("/set_firmware_url", handleSetFirmwareURL);
  server.on("/do_update", handleDoUpdate);
  server.on("/configure_ip", handleConfigureIP);
  server.on("/dst", handleDST);  // Neue Route für DST-Einstellung
  server.on("/timezone", handleTimezone);
  server.on("/dim_brightness", handleDimBrightness);

  server.begin();
  Serial.println("Webserver gestartet.");

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "Sketch" : "Filesystem";
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

  firmwareURL = preferences.getString(
    "firmwareURL",
    "https://github.com/Morpheus2510/Wordclock-1616-Matrix/releases/download/v1_9_9/WordClock_v1_9_10.ino.bin");

  Serial.println("Aktuelle Firmware-URL (aus Preferences oder Default): " + firmwareURL);
  clearLEDs();
  initialized = true;

  // NEU: Jetzt merken wir uns, wann Setup fertig ist
  wifiIndicatorStartTime = millis();

  ledStartTest();

  // DST-Status aus Preferences laden
  isDST = preferences.getBool("isDST", false);
}

// ========================================================
//   Hauptschleife
// ========================================================
void loop() {
  server.handleClient();
  ArduinoOTA.handle();

  // Jede Sekunde displayTime() aufrufen
  static unsigned long lastDisplayTime = 0;
  static unsigned long lastWiFiCheck = 0;
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastDisplayTime >= 1000) {
    lastDisplayTime = currentMillis;
    displayTime();
  }

  // Alle 30 Sekunden WLAN-Verbindung prüfen und ggf. neu verbinden
  if (currentMillis - lastWiFiCheck >= 30000) {
    lastWiFiCheck = currentMillis;
    if (WiFi.status() != WL_CONNECTED) {
      tryWiFiConnection();
    }
  }

  // Alle 60 Sekunden prüfen, ob WLAN da ist => DS3231 ggf. nachstellen
  static unsigned long lastConnectionCheck = 0;
  const unsigned long connectionCheckInterval = 60000;
  if (currentMillis - lastConnectionCheck >= connectionCheckInterval) {
    lastConnectionCheck = currentMillis;

    // Wenn WLAN verbunden => NTP lesen => DS3231 sync
    if (WiFi.status() == WL_CONNECTED) {
      struct tm ntpTime;
      if (getLocalTime(&ntpTime)) {
        // DS3231 aktualisieren
        if (rtcAvailable) {
          rtc.adjust(DateTime(
            ntpTime.tm_year + 1900,
            ntpTime.tm_mon + 1,
            ntpTime.tm_mday,
            ntpTime.tm_hour,
            ntpTime.tm_min,
            ntpTime.tm_sec));
          Serial.println("DS3231 mit NTP-Zeit periodisch aktualisiert.");
        }
        // Software-Uhr aktualisieren
        softwareTime = mktime(&ntpTime);
        lastMillisForSoftwareClock = millis();
        isTimeValid = true;
      } else {
        Serial.println("NTP-Server nicht erreichbar, DS3231 bleibt unverändert.");
      }
    } else {
      Serial.println("Kein WLAN, DS3231 übernimmt Zeit (falls vorhanden).");
    }
  }

  // ------------------------------------------------------
  // NEU: 30-Sekunden-Indikator-LEDs für WLAN-Status
  // ------------------------------------------------------
  if (showWifiIndicator) {
    unsigned long elapsed = currentMillis - wifiIndicatorStartTime;
    if (elapsed < 30000) {
      // In den ersten 30 Sekunden nach Setup:
      if (WiFi.status() == WL_CONNECTED) {
        // GRÜN
        setSingleLED(248, RgbColor(0, 255, 0));
        setSingleLED(247, RgbColor(0, 255, 0));
        setSingleLED(246, RgbColor(0, 255, 0));
        setSingleLED(245, RgbColor(0, 255, 0));
      } else {
        // ROT
        setSingleLED(248, RgbColor(255, 0, 0));
        setSingleLED(247, RgbColor(255, 0, 0));
        setSingleLED(246, RgbColor(255, 0, 0));
        setSingleLED(245, RgbColor(255, 0, 0));
      }
      strip.Show();
    } else {
      // Nach 30 Sek ausschalten und nie wieder ändern
      setSingleLED(248, RgbColor(0, 0, 0));
      setSingleLED(247, RgbColor(0, 0, 0));
      setSingleLED(246, RgbColor(0, 0, 0));
      setSingleLED(245, RgbColor(0, 0, 0));
      strip.Show();

      showWifiIndicator = false;
    }
  }
}