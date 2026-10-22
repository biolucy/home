// =====================================================================
// UK Rail Departures Board - Freenove FNK0114 (ESP32 + SPI TFT)
// Fixed low backlight + full station/destination names + bottom clock
//
// Requires:
//   - TFT_eSPI with the matching Freenove setup for your FNK0114
//   - ArduinoJson 6.x
//   - Board: ESP32 Dev Module
// =====================================================================

#include <WiFi.h>
#include <esp_eap_client.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <Preferences.h>

struct Departure {
  String std;
  String etd;
  String destination;
  String platform;
};

// ----------------------- CONFIG -------------------------------------
const bool USE_ENTERPRISE_WIFI = true;

const char* WIFI_SSID = "";
const char* WIFI_PASS = "";

// Enterprise WiFi credentials are NOT stored in this sketch. They are
// entered once over the Serial Monitor and saved in the ESP32's flash (NVS).
const char* EAP_SSID = "eduroam";

// Change this to another Darwin/National Rail CRS code.
const char* STATION_CRS = "UNI";

// Optional display override. Leave empty to use the API's station name.
// Example: "Birmingham New Street"
const char* STATION_DISPLAY_NAME = "";

// Optional destination filter. Leave empty to show all departures.
const char* DEST_CRS = "BHM";

const char* RDG_TOKEN = "";
const unsigned long REFRESH_MS = 30000;

// Fixed, relatively low brightness. Range: 0-255.
const uint8_t BRIGHTNESS_LOW = 128;

// Clock settings. UK time with automatic GMT/BST switching.
const char* TIME_ZONE = "GMT0BST,M3.5.0/1,M10.5.0";
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.google.com";
const int CLOCK_H = 16;  // height of the clock strip at the bottom (size-2 text)

#ifndef TFT_BL
#define TFT_BL 32
#endif

const int MAX_DEPARTURES = 6;

TFT_eSPI tft = TFT_eSPI();
int lastClockSec = -1;

// Shared between the background fetch task and the main (drawing) loop.
SemaphoreHandle_t dataMutex;
Departure fetchedDeps[MAX_DEPARTURES];
int fetchedCount = 0;
String fetchedName;
bool newDataReady = false;
bool fetchFailed = false;
bool haveData = false;

Departure currentDeps[MAX_DEPARTURES];
int currentCount = 0;
String currentStationName;

#define COL_BG TFT_BLACK
#define COL_HEADER TFT_YELLOW
#define COL_TEXT TFT_WHITE
#define COL_GOOD TFT_GREEN
#define COL_BAD TFT_RED
#define COL_DIM TFT_SKYBLUE

// ----------------------- SAVED CREDENTIALS --------------------------
Preferences prefs;
String eapIdentity, eapUsername, eapPassword;

// Reads one line from the Serial Monitor (nothing is echoed back by the
// board, so the password is never displayed). Empty lines are ignored
// unless allowEmpty is true.
String readSerialLine(bool allowEmpty = false) {
  String line;
  for (;;) {
    while (!Serial.available()) delay(10);
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (line.length() > 0 || allowEmpty) break;
      continue;
    }
    if ((c == 8 || c == 127) && line.length() > 0) {
      line.remove(line.length() - 1);
      continue;
    }
    if (c >= 32 && c < 127) line += c;
  }
  // Swallow the second half of a CR+LF pair.
  delay(50);
  while (Serial.available()) {
    char p = Serial.peek();
    if (p == '\r' || p == '\n') Serial.read();
    else break;
  }
  return line;
}

void promptCredentials() {
  tft.fillScreen(COL_BG);
  tft.setTextSize(2);
  tft.setTextColor(COL_HEADER, COL_BG);
  tft.setCursor(8, 10);
  tft.print("WiFi setup");
  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setCursor(8, 40);
  tft.print("Open the Serial Monitor");
  tft.setCursor(8, 54);
  tft.print("(115200 baud) and follow");
  tft.setCursor(8, 68);
  tft.print("the prompts.");

  while (Serial.available()) Serial.read();
  Serial.println();
  Serial.println("=== WiFi credential setup ===");
  Serial.println("Set the Serial Monitor line ending to 'Newline' or 'Both NL & CR'.");

  Serial.print("Username (e.g. abc123@uni.ac.uk): ");
  eapUsername = readSerialLine();
  Serial.println(eapUsername);

  Serial.print("Outer/anonymous identity (press Enter to reuse username): ");
  eapIdentity = readSerialLine(true);
  if (eapIdentity.length() == 0) eapIdentity = eapUsername;
  Serial.println(eapIdentity);

  Serial.print("Password (not shown as you type): ");
  eapPassword = readSerialLine();
  Serial.printf("(%u characters received)\n", (unsigned)eapPassword.length());

  prefs.begin("eap", false);
  prefs.putString("user", eapUsername);
  prefs.putString("ident", eapIdentity);
  prefs.putString("pass", eapPassword);
  prefs.end();

  Serial.println("Saved to flash. Continuing...");
}

void loadCredentials() {
  prefs.begin("eap", true);
  eapUsername = prefs.getString("user", "");
  eapIdentity = prefs.getString("ident", "");
  eapPassword = prefs.getString("pass", "");
  prefs.end();

  bool missing = eapUsername.length() == 0 || eapPassword.length() == 0;
  bool wantChange = false;

  if (!missing) {
    if (eapIdentity.length() == 0) eapIdentity = eapUsername;

    tft.fillScreen(COL_BG);
    tft.setTextSize(1);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.setCursor(8, 10);
    tft.print("Saved WiFi login found.");
    tft.setCursor(8, 26);
    tft.print("Press BOOT or send 'c' over");
    tft.setCursor(8, 40);
    tft.print("Serial within 3s to change it.");

    Serial.println("Saved WiFi credentials found.");
    Serial.println("Send 'c' within 3 seconds (or press the BOOT button) to change them.");

    pinMode(0, INPUT_PULLUP);
    while (Serial.available()) Serial.read();
    unsigned long start = millis();
    while (millis() - start < 3000) {
      if (digitalRead(0) == LOW) { wantChange = true; break; }
      if (Serial.available() && (Serial.read() | 0x20) == 'c') { wantChange = true; break; }
      delay(10);
    }
  }

  if (missing || wantChange) promptCredentials();
}

void connectWiFi() {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Connecting WiFi...");

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);

  if (USE_ENTERPRISE_WIFI) {
    esp_eap_client_set_identity((const uint8_t*)eapIdentity.c_str(), eapIdentity.length());
    esp_eap_client_set_username((const uint8_t*)eapUsername.c_str(), eapUsername.length());
    esp_eap_client_set_password((const uint8_t*)eapPassword.c_str(), eapPassword.length());
    esp_wifi_sta_enterprise_enable();
    WiFi.begin(EAP_SSID);
  } else {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }

  int dots = 0;
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    tft.fillRect(10, 40, tft.width() - 20, 20, COL_BG);
    tft.setCursor(10, 40);
    for (int i = 0; i <= dots % 4; i++) tft.print(".");
    dots++;

    if (millis() - startAttempt > 20000) {
      startAttempt = millis();
      if (USE_ENTERPRISE_WIFI) WiFi.begin(EAP_SSID);
      else WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }

  tft.fillScreen(COL_BG);
  tft.setCursor(10, 10);
  tft.print("WiFi connected!");
  delay(800);
}

bool fetchDepartures(Departure* out, int maxCount, int& count, String& stationName) {
  count = 0;
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = "https://huxley2.azurewebsites.net/departures/" + String(STATION_CRS);
  if (strlen(DEST_CRS) > 0) url += "/to/" + String(DEST_CRS);
  url += "/" + String(maxCount);
  if (strlen(RDG_TOKEN) > 0 && String(RDG_TOKEN) != "YOUR_RAIL_DELIVERY_GROUP_DARWIN_TOKEN") {
    url += "?accessToken=" + String(RDG_TOKEN);
  }

  http.begin(client, url);
  int httpCode = http.GET();
  Serial.printf("HTTP GET %s -> code %d\n", url.c_str(), httpCode);

  if (httpCode != 200) {
    Serial.println(http.getString());
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(12288);
  if (deserializeJson(doc, payload)) return false;

  stationName = doc["locationName"].as<String>();
  JsonArray services = doc["trainServices"];
  if (services.isNull()) return true;

  for (JsonObject svc : services) {
    if (count >= maxCount) break;

    Departure d;
    d.std = svc["std"].as<String>();
    d.etd = svc["etd"].as<String>();

    JsonArray destinations = svc["destination"];
    if (!destinations.isNull() && destinations.size() > 0) {
      d.destination = destinations[0]["locationName"].as<String>();
      if (d.destination.length() == 0) {
        d.destination = destinations[0]["crs"].as<String>();
      }
    } else {
      d.destination = "Unknown destination";
    }

    d.platform = svc["platform"].as<String>();
    if (d.platform.length() == 0) d.platform = "TBC";

    out[count++] = d;
  }

  return true;
}

String stationTitle(const String& returnedName) {
  if (strlen(STATION_DISPLAY_NAME) > 0) return String(STATION_DISPLAY_NAME);
  if (returnedName.length() > 0) return returnedName;
  return String(STATION_CRS);
}

// Shortens only when the text physically cannot fit on the available line.
String fitText(const String& input, int maxWidth) {
  if (tft.textWidth(input) <= maxWidth) return input;

  String result = input;
  while (result.length() > 1 && tft.textWidth(result + "...") > maxWidth) {
    result.remove(result.length() - 1);
  }
  return result + "...";
}

// Draws the clock strip at the bottom of the screen: date on the left,
// time (HH:MM:SS) on the right. Only redraws when the second changes,
// unless force is true (e.g. after the screen has been cleared).
void drawClock(bool force = false) {
  struct tm t;
  bool synced = getLocalTime(&t, 0) && t.tm_year > 120;  // year > 2020

  if (synced) {
    if (!force && t.tm_sec == lastClockSec) return;
    lastClockSec = t.tm_sec;
  } else {
    if (!force && lastClockSec == -2) return;
    lastClockSec = -2;
  }

  char dateBuf[16];
  char timeBuf[16];
  if (synced) {
    strftime(dateBuf, sizeof(dateBuf), "%a %d %b", &t);
    strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &t);
  } else {
    strcpy(dateBuf, "");
    strcpy(timeBuf, "--:--:--");
  }

  const int W = tft.width();
  const int H = tft.height();
  const int margin = 6;
  const int y = H - CLOCK_H;

  tft.setTextWrap(false);
  tft.setTextSize(2);
  tft.fillRect(0, y, W, CLOCK_H, COL_BG);

  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(margin, y);
  tft.print(dateBuf);

  tft.setTextColor(COL_HEADER, COL_BG);
  int tw = tft.textWidth(timeBuf);
  tft.setCursor(W - margin - tw, y);
  tft.print(timeBuf);
}

void drawBoard(Departure* deps, int count, const String& stationName) {
  const int W = tft.width();
  const int H = tft.height();
  const int margin = 6;
  const int boardBottom = H - CLOCK_H;  // leave room for the clock

  // Clear only the board area so the clock strip is left untouched.
  tft.fillRect(0, 0, W, boardBottom, COL_BG);
  tft.setTextWrap(false);

  tft.setTextSize(2);
  tft.setTextColor(COL_HEADER, COL_BG);
  tft.setCursor(margin, 4);
  tft.print(fitText(stationTitle(stationName), W - 2 * margin));
  tft.drawFastHLine(0, 25, W, COL_DIM);

  if (count == 0) {
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.setCursor(margin, 42);
    tft.print("No services found");
    tft.drawFastHLine(0, boardBottom - 2, W, COL_DIM);
    return;
  }

  // Two-line rows: time + destination, then platform + expected time.
  // This keeps full names readable rather than squeezing four columns.
  const int rowH = 39;
  const int yStart = 29;
  const int xTime = margin;
  const int xInfo = 92;
  const int infoWidth = W - xInfo - margin;

  for (int i = 0; i < count; i++) {
    int y = yStart + i * rowH;
    if (y + rowH > boardBottom) break;

    tft.setTextSize(2);
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.setCursor(xTime, y + 2);
    tft.print(deps[i].std);

    tft.setCursor(xInfo, y + 2);
    tft.print(fitText(deps[i].destination, infoWidth));

    tft.setTextSize(1);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.setCursor(xInfo, y + 23);
    tft.print("Platform: ");
    tft.print(deps[i].platform);
    tft.print("   Expected: ");

    uint16_t expectedColour = (deps[i].etd == "On time") ? COL_GOOD : COL_BAD;
    tft.setTextColor(expectedColour, COL_BG);
    tft.print(deps[i].etd);

    if (i < count - 1) {
      tft.drawFastHLine(0, y + rowH - 2, W, COL_DIM);
    }
  }
}

// Runs forever on its own task: fetch, hand results to the main loop, sleep.
void fetchTask(void* param) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      Departure tmp[MAX_DEPARTURES];
      int count = 0;
      String name;
      bool ok = fetchDepartures(tmp, MAX_DEPARTURES, count, name);

      xSemaphoreTake(dataMutex, portMAX_DELAY);
      if (ok) {
        for (int i = 0; i < count; i++) fetchedDeps[i] = tmp[i];
        fetchedCount = count;
        fetchedName = name;
        newDataReady = true;
        fetchFailed = false;
      } else {
        fetchFailed = true;
      }
      xSemaphoreGive(dataMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(REFRESH_MS));
  }
}

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(COL_BG);

  // Fixed low brightness; no touch or dimming logic is used.
  ledcAttach(TFT_BL, 5000, 8);
  ledcWrite(TFT_BL, BRIGHTNESS_LOW);

  if (USE_ENTERPRISE_WIFI) loadCredentials();
  connectWiFi();

  // Start NTP time sync with UK timezone (handles GMT/BST automatically).
  configTzTime(TIME_ZONE, NTP_SERVER_1, NTP_SERVER_2);

  // Initial placeholder until the first fetch completes.
  tft.fillScreen(COL_BG);
  tft.setTextSize(2);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(8, 6);
  tft.print("Loading departures...");

  // Fetch departures in a background task so the display (and clock)
  // never freezes or gets blanked while the network request runs.
  dataMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(fetchTask, "fetchTask", 16384, NULL, 1, NULL, 0);
}

// Small red "!" in the top-right corner when the latest refresh failed.
// The previous board stays on screen underneath.
void drawFetchWarning(bool show) {
  tft.setTextSize(2);
  tft.setTextColor(show ? COL_BAD : COL_BG, COL_BG);
  tft.setCursor(tft.width() - 18, 4);
  tft.print("!");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    // Restore the screen after the reconnect screen wiped it.
    tft.fillScreen(COL_BG);
    if (haveData) drawBoard(currentDeps, currentCount, currentStationName);
    lastClockSec = -1;
  }

  // Pick up results from the background task, if any (non-blocking).
  if (xSemaphoreTake(dataMutex, 0) == pdTRUE) {
    if (newDataReady) {
      for (int i = 0; i < fetchedCount; i++) currentDeps[i] = fetchedDeps[i];
      currentCount = fetchedCount;
      currentStationName = fetchedName;
      newDataReady = false;
      haveData = true;
      xSemaphoreGive(dataMutex);
      drawBoard(currentDeps, currentCount, currentStationName);  // clears the warning too
    } else if (fetchFailed) {
      fetchFailed = false;
      xSemaphoreGive(dataMutex);
      if (haveData) {
        drawFetchWarning(true);
      } else {
        tft.fillRect(0, 0, tft.width(), tft.height() - CLOCK_H, COL_BG);
        tft.setTextSize(2);
        tft.setTextColor(COL_BAD, COL_BG);
        tft.setCursor(8, 10);
        tft.print("Fetch failed - retrying");
      }
    } else {
      xSemaphoreGive(dataMutex);
    }
  }

  drawClock();  // only repaints when the second changes

  delay(100);
}
